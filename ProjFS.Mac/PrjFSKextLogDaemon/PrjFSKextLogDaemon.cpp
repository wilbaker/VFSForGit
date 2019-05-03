#include "../PrjFSKext/public/PrjFSLogClientShared.h"
#include "../PrjFSKext/public/PrjFSVnodeCacheHealth.h"
#include "../PrjFSLib/PrjFSUser.hpp"
#include <iostream>
#include <OS/log.h>
#include <IOKit/IOKitLib.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

using std::string;

static const char PrjFSKextLogDaemon_OSLogSubsystem[] = "org.vfsforgit.prjfs.PrjFSKextLogDaemon";
static const int INVALID_SOCKET_FD = -1;

static os_log_t s_daemonLogger, s_kextLogger;
static IONotificationPortRef s_notificationPort;
static int s_messageListenerSocket = INVALID_SOCKET_FD;
static string s_messageListenerSocketPath = "/usr/local/GitService/pipe/git-c780ac06-135a-4e9e-ab6c-d41e2d265baa";

static void StartLoggingKextMessages(io_connect_t connection, io_service_t service, os_log_t daemonLogger, os_log_t kextLogger);
static void SetupExitSignalHandler();
static dispatch_source_t StartKextHealthDataPolling(io_connect_t connection);
static bool TryFetchAndLogKextHealthData(io_connect_t connection);

static void CreatePipeToMessageListener();
static void WriteToMessageListener(const string& message);

int main(int argc, const char* argv[])
{
#ifdef DEBUG
    printf("PrjFSKextLogDaemon starting up");
    fflush(stdout);
#endif
    
    s_daemonLogger = os_log_create(PrjFSKextLogDaemon_OSLogSubsystem, "daemon");
    s_kextLogger = os_log_create(PrjFSKextLogDaemon_OSLogSubsystem, "kext");
    
    os_log(s_daemonLogger, "PrjFSKextLogDaemon starting up");

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

    CreatePipeToMessageListener();
    WriteToMessageListener("Test message in startup");

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
        30 /* * 60 */ * NSEC_PER_SEC, // interval
        10 * NSEC_PER_SEC);     // leeway
    dispatch_source_set_event_handler(timer, ^{
        CreatePipeToMessageListener();
        WriteToMessageListener("Test message in timer");
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
    }
    else
    {
        fprintf(stderr, "fetching profiling data from kernel failed: 0x%x\n", ret);
        return false;
    }
    
    return true;
}

static void CreatePipeToMessageListener()
{
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
    size_t resultLength = strlcpy(socket_address.sun_path, s_messageListenerSocketPath.c_str(), sizeof(socket_address.sun_path));
    
    if (resultLength >= sizeof(socket_address.sun_path))
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "Could not copy socket path: %s, insufficient buffer. resultLength: %lu, sizeof(socket_address.sun_path): %lu",
            s_messageListenerSocketPath.c_str(),
            resultLength,
            sizeof(socket_address.sun_path));
        
        goto ClosePipeAndCleanup;
    }
    
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

static void WriteToMessageListener(const string& message)
{
    if (INVALID_SOCKET_FD == s_messageListenerSocket)
    {
        return;
    }

    string jsonMessage = "{\\\"version\\\":\\\"0.2.173.2\\\",\\\"providerName\\\":\\\"Microsoft.Git.GVFS\\\",\\\"eventName\\\":\\\"PrjFSKextLogDaemon\\\",\\\"eventLevel\\\":2,\\\"eventOpcode\\\":0,\\\"payload\\\":{\\\"enlistmentId\\\":null,\\\"mountId\\\":null,\\\"gitCommandSessionId\\\":null,\\\"json\\\":\\\"{\\\\\\\"Version\\\\\\\":\\\\\\\"0.2.173.2\\\\\\\",\\\\\\\"Message\\\\\\\":\\\\\\\"" + message + "\\\\\\\"}\\\"}}";

    size_t bytesWritten;
    
    do
    {
        bytesWritten = write(s_messageListenerSocket, jsonMessage.c_str(), jsonMessage.length());
    } while (bytesWritten == -1 && errno == EINTR);
    
    int error = errno;
    if (bytesWritten != jsonMessage.length())
    {
        os_log_with_type(
            s_kextLogger,
            OS_LOG_TYPE_DEFAULT,
            "Failed to write message '%s' to listener.  Error: %d, Bytes written: %lu",
            jsonMessage.c_str(),
            error,
            bytesWritten);
    }
}
