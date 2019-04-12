#include "../PrjFSKext/public/PrjFSLogClientShared.h"
#include "../PrjFSLib/PrjFSUser.hpp"
#include <iostream>
#include <OS/log.h>
#include <IOKit/IOKitLib.h>
#include <signal.h>

static const char PrjFSKextLogDaemon_OSLogSubsystem[] = "org.vfsforgit.prjfs.PrjFSKextLogDaemon";

static os_log_t s_daemonLogger, s_kextLogger;
static IONotificationPortRef s_notificationPort;

static void StartLoggingKextMessages(io_connect_t connection, io_service_t service, os_log_t daemonLogger, os_log_t kextLogger);
static void SetupExitSignalHandler();
static dispatch_source_t StartKextHealthDataPolling(io_connect_t connection);
static bool TryFetchAndLogKextHealthData(io_connect_t connection);

int main(int argc, const char* argv[])
{
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

    PrjFSService_WatchForServiceTermination(
        prjfsService,
        s_notificationPort,
        [prjfsServiceEntryID, connection, logDataQueue]()
        {
            DataQueue_Dispose(logDataQueue.get(), connection, LogMemoryType_MessageQueue);

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
    dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, 1 * 30 * NSEC_PER_SEC, 10 * NSEC_PER_SEC);
    dispatch_source_set_event_handler(timer, ^{
        TryFetchAndLogKextHealthData(connection);
    });
    dispatch_resume(timer);
    return timer;
}

static bool TryFetchAndLogKextHealthData(io_connect_t connection)
{
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        mach_timebase_info(&s_machTimebase);
    });
    
    PrjFSPerfCounterResult counters[PrjFSPerfCounter_Count];
    size_t out_size = sizeof(counters);
    IOReturn ret = IOConnectCallStructMethod(connection, LogSelector_FetchProfilingData, nullptr, 0, counters, &out_size);
    if (ret == kIOReturnUnsupported)
    {
        return false;
    }
    else if (ret == kIOReturnSuccess)
    {
        for (int32_t i = 0; i < PrjFSPerfCounter_Count; ++i)
        {
            double numSamples = counters[i].numSamples;
            printf(
                "%2u %-35s [%10llu]",
                i,
                PerfCounterNames[i],
                counters[i].numSamples);
            
            if (counters[i].min != UINT64_MAX)
            {
                // The values on the counter are reported in units of mach absolute time
                double sum = counters[i].sum;

                uint64_t sumNS = nanosecondsFromAbsoluteTime(sum);
                uint64_t meanNS = numSamples > 0 ? sumNS / numSamples : 0;

                printf(
                    "[%15llu][%10llu][%10llu][%10llu]",
                    sumNS,
                    meanNS,
                    nanosecondsFromAbsoluteTime(counters[i].min),
                    nanosecondsFromAbsoluteTime(counters[i].max));
                
                static const char* const barGraphItems[9] = {
                    NBSP_STR, "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█",
                };
                
                // Find the bucket with the largest number of samples; use the 8/8
                // bar symbol for that, and make all other buckets relative to it.
                // (Defining the overall number of samples across all buckets as
                // 100% on the scale would limit the resolution of the information
                // you could read from the graph; the 8/8 bar symbol would only be
                // used on distributions very concentrated on one bucket.)
                _Atomic uint64_t(&buckets)[PrjFSPerfCounterBuckets] = counters[i].sampleBuckets;
                uint64_t bucketMax = *std::max_element(begin(buckets), end(buckets));
                if (bucketMax > 0) // Should normally not be 0 if we get here, but defends against divide by 0 in case of a bug
                {
                    printf("[");
                    for (size_t bucket = 0; bucket < PrjFSPerfCounterBuckets; ++bucket)
                    {
                        // Always round up so we have a clear distinction between buckets with zero and even a single item.
                        uint64_t eighths = (8 * buckets[bucket] + bucketMax - 1) / bucketMax;
                        assert(eighths >= 0);
                        assert(eighths <= 8);
                        printf("%s", barGraphItems[eighths]);
                    }
                    printf("]");
                }
            }
            printf("\n");
        }
    }
    else
    {
        fprintf(stderr, "fetching profiling data from kernel failed: 0x%x\n", ret);
        return false;
    }
    
    printf("\n");
    fflush(stdout);
    
    return true;
}
