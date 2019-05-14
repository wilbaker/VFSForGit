#include "../PrjFSKext/public/PrjFSLogClientShared.h"
#include "../PrjFSKext/public/PrjFSVnodeCacheHealth.h"
#include "../PrjFSLib/PrjFSUser.hpp"
#include <iostream>
#include <mutex>
#include <OS/log.h>
#include <IOKit/IOKitLib.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

using std::lock_guard;
using std::mutex;
using std::string;
using std::to_string;

static const char PrjFSKextLogDaemon_OSLogSubsystem[] = "org.vfsforgit.prjfs.PrjFSKextLogDaemon";
static const int INVALID_SOCKET_FD = -1;

static os_log_t s_daemonLogger, s_kextLogger;
static IONotificationPortRef s_notificationPort;

static int s_messageListenerSocket = INVALID_SOCKET_FD;
static string s_messageListenerSocketPath = "/usr/local/GitService/pipe/vfs-c780ac06-135a-4e9e-ab6c-d41e2d265baa";
static mutex s_messageListenerMutex;

static void StartLoggingKextMessages(io_connect_t connection, io_service_t service, os_log_t daemonLogger, os_log_t kextLogger);
static void SetupExitSignalHandler();
static dispatch_source_t StartKextHealthDataPolling(io_connect_t connection);
static bool TryFetchAndLogKextHealthData(io_connect_t connection);

static void CreatePipeToMessageListener();
static void WriteHealthDataToMessageListener(const PrjFSVnodeCacheHealth& healthData);
static void WriteInfoMessageToMessageListener(const string& message);
static void WriteErrorMessageToMessageListener(const string& message, const IOReturn ioReturn);
static void WriteJsonToMessageListener(const string& eventName, const string& jsonMessage);

int main(int argc, const char* argv[])
{
#ifdef DEBUG
    printf("PrjFSKextLogDaemon starting up");
    fflush(stdout);
#endif
    
    s_daemonLogger = os_log_create(PrjFSKextLogDaemon_OSLogSubsystem, "daemon");
    s_kextLogger = os_log_create(PrjFSKextLogDaemon_OSLogSubsystem, "kext");
    
    os_log(s_daemonLogger, "PrjFSKextLogDaemon starting up");
    
    CreatePipeToMessageListener();
    WriteInfoMessageToMessageListener("PrjFSKextLogDaemon starting up");

    s_notificationPort = IONotificationPortCreate(kIOMasterPortDefault);
    IONotificationPortSetDispatchQueue(s_notificationPort, dispatch_get_main_queue());

    PrjFSService_WatchContext* watchContext = PrjFSService_WatchForServiceAndConnect(
         s_notificationPort,
         UserClientType_Log,
         [](io_service_t service, io_connect_t connection, bool serviceVersionMismatch, IOReturn connectResult, PrjFSService_WatchContext* context)
         {
             if (connectResult != kIOReturnSuccess || connection == IO_OBJECT_NULL)
             {
                io_string_t servicePath = "";
                IORegistryEntryGetPath(service, kIOServicePlane, servicePath);
                if (serviceVersionMismatch)
                {
                    CFTypeRef kextVersionObj = IORegistryEntryCreateCFProperty(service, CFSTR(PrjFSKextVersionKey), kCFAllocatorDefault, 0);
                    os_log_error(
                        s_daemonLogger,
                        "Failed to connect to newly matched PrjFS kernel service at '%{public}s'; version mismatch. Expected %{public}s, kernel service version %{public}@",
                        servicePath, PrjFSKextVersion, kextVersionObj);
                    if (kextVersionObj != nullptr)
                    {
                        CFRelease(kextVersionObj);
                    }
                }
                else
                {
                    os_log_error(
                        s_daemonLogger,
                        "Failed to connect to newly matched PrjFS kernel service at '%{public}s'; connecting failed with error 0x%x",
                        servicePath, connectResult);
                }
             }
             else
             {
                 StartLoggingKextMessages(connection, service, s_daemonLogger, s_kextLogger);
             }
         });
    if (nullptr == watchContext)
    {
        os_log_error(s_daemonLogger, "Failed to register for service notifications.");
        return 1;
    }
    
    SetupExitSignalHandler();

    os_log(s_daemonLogger, "PrjFSKextLogDaemon running");

    CFRunLoopRun();

    os_log(s_daemonLogger, "PrjFSKextLogDaemon shutting down");

    PrjFSService_StopWatching(watchContext);
    
    return 0;
}

static os_log_type_t KextLogLevelAsOSLogType(KextLog_Level level)
{
    switch (level)
    {
    case KEXTLOG_INFO:
        return OS_LOG_TYPE_INFO;
    case KEXTLOG_DEFAULT:
        return OS_LOG_TYPE_DEFAULT;
    case KEXTLOG_ERROR:
    default:
        return OS_LOG_TYPE_ERROR;
    }
}

static void StartLoggingKextMessages(io_connect_t connection, io_service_t prjfsService, os_log_t daemonLogger, os_log_t kextLogger)
{
    uint64_t prjfsServiceEntryID = 0;
    IORegistryEntryGetRegistryEntryID(prjfsService, &prjfsServiceEntryID);

    std::shared_ptr<DataQueueResources> logDataQueue(new DataQueueResources {});
    if (!PrjFSService_DataQueueInit(logDataQueue.get(), connection, LogPortType_MessageQueue, LogMemoryType_MessageQueue, dispatch_get_main_queue()))
    {
        os_log_error(s_daemonLogger, "Failed to set up log message data queue an connection to service with registry entry 0x%llx", prjfsServiceEntryID);
        IOServiceClose(connection);
        return;
    }

    os_log(s_daemonLogger, "Started logging kext messages from PrjFS IOService with registry entry id 0x%llx", prjfsServiceEntryID);

    dispatch_source_set_event_handler(logDataQueue->dispatchSource, ^{
        DataQueue_ClearMachNotification(logDataQueue->notificationPort);
        
        while (IODataQueueEntry* entry = DataQueue_Peek(logDataQueue->queueMemory))
        {
            int messageSize = entry->size;
            if (messageSize >= sizeof(KextLog_MessageHeader) + 2)
            {
                struct KextLog_MessageHeader message = {};
                memcpy(&message, entry->data, sizeof(KextLog_MessageHeader));
                int logStringLength = messageSize - sizeof(KextLog_MessageHeader) - 1;
                os_log_type_t messageLogType = KextLogLevelAsOSLogType(message.level);
                os_log_with_type(s_kextLogger, messageLogType, "%{public}.*s", logStringLength, entry->data + sizeof(KextLog_MessageHeader));
            }
            else
            {
                os_log_error(s_daemonLogger, "Malformed message received from kext. messageSize = %d, expecting %zu or more", messageSize, sizeof(KextLog_MessageHeader) + 2);
            }

            DataQueue_Dequeue(logDataQueue->queueMemory, nullptr, nullptr);
        }
    });
    dispatch_resume(logDataQueue->dispatchSource);

    dispatch_source_t timer = nullptr;
    if (TryFetchAndLogKextHealthData(connection))
    {
        timer = StartKextHealthDataPolling(connection);
    }

    PrjFSService_WatchForServiceTermination(
        prjfsService,
        s_notificationPort,
        [prjfsServiceEntryID, connection, logDataQueue, timer]()
        {
            DataQueue_Dispose(logDataQueue.get(), connection, LogMemoryType_MessageQueue);

            if (nullptr != timer)
            {
                dispatch_cancel(timer);
                dispatch_release(timer);
            }

            IOServiceClose(connection);
            os_log(s_daemonLogger, "Stopped logging kext messages from PrjFS IOService with registry entry id 0x%llx", prjfsServiceEntryID);
        });
}

static void HandleSigterm(int sig, siginfo_t* info, void* uc)
{
    // Does nothing, unlike the default implementation which immediately aborts exits the process
}

// Sets up handling of SIGTERM so that shutting down the daemon can be logged (at the end of main())
// This is for clarity, so that we know if absence of logs is because there was nothing logged, or because the log daemon shut down.
static void SetupExitSignalHandler()
{
    dispatch_source_t signalSource =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0 /* no mask values for signal dispatch sources */, dispatch_get_main_queue());
    dispatch_source_set_event_handler(signalSource, ^{
        CFRunLoopStop(CFRunLoopGetMain());
    });
    dispatch_resume(signalSource);
    
    struct sigaction newAction = { .sa_sigaction = HandleSigterm };
    struct sigaction oldAction = {};
    sigaction(SIGTERM, &newAction, &oldAction);
}

static dispatch_source_t StartKextHealthDataPolling(io_connect_t connection)
{
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(
        timer,
        DISPATCH_TIME_NOW,      // start
        30 * 60 * NSEC_PER_SEC, // interval
        10 * NSEC_PER_SEC);     // leeway
    dispatch_source_set_event_handler(timer, ^{
        // Every time the timer fires attempt to connect (if not already connected)
        CreatePipeToMessageListener();
        TryFetchAndLogKextHealthData(connection);
    });
    dispatch_resume(timer);
    return timer;
}

static bool TryFetchAndLogKextHealthData(io_connect_t connection)
{
    PrjFSVnodeCacheHealth healthData;
    size_t out_size = sizeof(healthData);
    IOReturn ret = IOConnectCallStructMethod(connection, LogSelector_FetchVnodeCacheHealth, nullptr, 0, &healthData, &out_size);
    if (ret == kIOReturnUnsupported)
    {
        WriteErrorMessageToMessageListener("IOConnectCallStructMethod failed for LogSelector_FetchVnodeCacheHealth", ret);
        return false;
    }
    else if (ret == kIOReturnSuccess)
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "PrjFS Vnode Cache Health: CacheCapacity=%u, CacheEntries=%u, InvalidationCount=%llu, CacheLookups=%llu, LookupCollisions=%llu, FindRootHits=%llu, FindRootMisses=%llu, RefreshRoot=%llu, InvalidateRoot=%llu",
            healthData.cacheCapacity,
            healthData.cacheEntries,
            healthData.invalidateEntireCacheCount,
            healthData.totalCacheLookups,
            healthData.totalLookupCollisions,
            healthData.totalFindRootForVnodeHits,
            healthData.totalFindRootForVnodeMisses,
            healthData.totalRefreshRootForVnode,
            healthData.totalInvalidateVnodeRoot);
        
        WriteHealthDataToMessageListener(healthData);
    }
    else
    {
        WriteErrorMessageToMessageListener("Fetching profiling data from kernel failed", ret);
        fprintf(stderr, "fetching profiling data from kernel failed: 0x%x\n", ret);
        return false;
    }
    
    return true;
}

static void CreatePipeToMessageListener()
{
    lock_guard<mutex> lock(s_messageListenerMutex);
    
    if (INVALID_SOCKET_FD != s_messageListenerSocket)
    {
        // Already connected
        return;
    }

    s_messageListenerSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s_messageListenerSocket < 0)
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "Failed to create a new socket, path: %s, error: %d",
            s_messageListenerSocketPath.c_str(),
            errno);
        
        s_messageListenerSocket = INVALID_SOCKET_FD;
        return;
    }
    
    struct sockaddr_un socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_un));
    
    socket_address.sun_family = AF_UNIX;
    size_t pathLength = s_messageListenerSocketPath.length();
    if (pathLength + 1 >= sizeof(socket_address.sun_path))
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "Could not copy socket path: %s, insufficient buffer. pathLength: %lu, sizeof(socket_address.sun_path): %lu",
            s_messageListenerSocketPath.c_str(),
            pathLength,
            sizeof(socket_address.sun_path));
        
        goto ClosePipeAndCleanup;
    }
    
    strlcpy(socket_address.sun_path, s_messageListenerSocketPath.c_str(), sizeof(socket_address.sun_path));
    
    if(0 == connect(s_messageListenerSocket, (struct sockaddr *) &socket_address, sizeof(struct sockaddr_un)))
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "Connected to message listener on socket '%s'",
            s_messageListenerSocketPath.c_str());
        return;
    }
    
    os_log_with_type(
        s_kextLogger,
        OS_LOG_TYPE_DEFAULT,
        "Failed to connect socket, pipeName: %s, error: %d",
        s_messageListenerSocketPath.c_str(),
        errno);
    
ClosePipeAndCleanup:

    if (INVALID_SOCKET_FD != s_messageListenerSocket)
    {
        close(s_messageListenerSocket);
        s_messageListenerSocket = INVALID_SOCKET_FD;
    }
}

static void WriteInfoMessageToMessageListener(const string& message)
{
    WriteJsonToMessageListener(
        "info",
        "{"
            "\"Message\":\"" + message + "\""
        "}");
}

static void WriteErrorMessageToMessageListener(const string& message, const IOReturn ioReturn)
{
    WriteJsonToMessageListener(
        "error",
        "{"
            "\"Message\":\"" + message + """\","
            "\"ret\":" + to_string(ioReturn) + + ""
        "}");
}

static void WriteHealthDataToMessageListener(const PrjFSVnodeCacheHealth& healthData)
{
    WriteJsonToMessageListener(
        "health",
        "{"
            "\"Message\":\"Vnode cache health\","
            "\"CacheCapacity\":" + to_string(healthData.cacheCapacity) + ","
            "\"CacheEntries\":" + to_string(healthData.cacheEntries) + ","
            "\"InvalidationCount\":" + to_string(healthData.invalidateEntireCacheCount) + ","
            "\"CacheLookups\":" + to_string(healthData.totalCacheLookups) + ","
            "\"LookupCollisions\":" + to_string(healthData.totalLookupCollisions) + ","
            "\"FindRootHits\":" + to_string(healthData.totalFindRootForVnodeHits) + ","
            "\"FindRootMisses\":" + to_string(healthData.totalFindRootForVnodeMisses) + ","
            "\"RefreshRoot\":" + to_string(healthData.totalRefreshRootForVnode) + ","
            "\"InvalidateRoot\":" + to_string(healthData.totalInvalidateVnodeRoot) + ""
        "}");
}

static void WriteJsonToMessageListener(const string& eventName, const string& jsonMessage)
{
    lock_guard<mutex> lock(s_messageListenerMutex);
    
    if (INVALID_SOCKET_FD == s_messageListenerSocket)
    {
        return;
    }

    // TODO: Properly version PrjFSKextLogDaemon and other Mac binaries
    string fullMessage =
    "{"
        "\"version\":\"0.6.XXX.X\","
        "\"providerName\":\"Microsoft.Git.GVFS\","
        "\"eventName\":\"kext." + eventName + "\","
        "\"payload\":" + jsonMessage + ""
    "}\n";

    size_t bytesWritten;
    do
    {
        bytesWritten = write(s_messageListenerSocket, fullMessage.c_str(), fullMessage.length());
    } while (bytesWritten == -1 && errno == EINTR);
    
    int error = errno;
    if (bytesWritten != fullMessage.length())
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "Failed to write message '%s' to listener.  Error: %d, Bytes written: %lu",
            fullMessage.c_str(),
            error,
            bytesWritten);

        // If anything goes wrong close the socket.  The next time the timer fires we'll re-connect
        close(s_messageListenerSocket);
        s_messageListenerSocket = INVALID_SOCKET_FD;
    }
}
