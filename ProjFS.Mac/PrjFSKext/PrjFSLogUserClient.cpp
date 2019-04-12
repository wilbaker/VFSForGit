#include "PrjFSLogUserClient.hpp"
#include "public/PrjFSLogClientShared.h"
#include "KextLog.hpp"
#include "public/PrjFSCommon.h"
#include "public/PrjFSHealthData.h"
#include "PerformanceTracing.hpp"
#include "VnodeCache.hpp"
#include <IOKit/IOSharedDataQueue.h>


OSDefineMetaClassAndStructors(PrjFSLogUserClient, IOUserClient);
// Amount of memory to set aside for kernel -> userspace log messages.
static const uint32_t LogMessageQueueCapacityBytes = 1024 * 1024;


static const IOExternalMethodDispatch LogUserClientDispatch[] =
{
    [LogSelector_FetchProfilingData] =
        {
            .function =                 &PrjFSLogUserClient::fetchProfilingData,
            .checkScalarInputCount =    0,
            .checkStructureInputSize =  0,
            .checkScalarOutputCount =   0,
            .checkStructureOutputSize = PrjFSPerfCounter_Count * sizeof(PrjFSPerfCounterResult), // array of results
        },
    [LogSelector_FetchHealthData] =
        {
            .function =                 &PrjFSLogUserClient::fetchHealthData,
            .checkScalarInputCount =    0,
            .checkStructureInputSize =  0,
            .checkScalarOutputCount =   0,
            .checkStructureOutputSize = sizeof(PrjFSHealthData),
        },
};


bool PrjFSLogUserClient::initWithTask(
    task_t owningTask,
    void* securityToken,
    UInt32 type,
    OSDictionary* properties)
{
    if (!this->super::initWithTask(owningTask, securityToken, type, properties))
    {
        return false;
    }
    
    this->dataQueueWriterMutex = Mutex_Alloc();
    if (!Mutex_IsValid(this->dataQueueWriterMutex))
    {
        this->cleanUp();
        return false;
    }
    
    this->dataQueue = IOSharedDataQueue::withCapacity(LogMessageQueueCapacityBytes);
    if (nullptr == this->dataQueue)
    {
        this->cleanUp();
        return false;
    }
    
    this->dataQueueMemory = this->dataQueue->getMemoryDescriptor();
    if (nullptr == this->dataQueueMemory)
    {
        this->cleanUp();
        return false;
    }
    
    this->logMessageDropped = false;
    return true;
    
}

void PrjFSLogUserClient::cleanUp()
{
    // clientClose() is not called if the user client class is terminated, e.g. from kextunload.
    // So deregister if we're still registered.
    KextLog_DeregisterUserClient(this);
    if (Mutex_IsValid(this->dataQueueWriterMutex))
    {
        Mutex_FreeMemory(&this->dataQueueWriterMutex);
    }
    
    OSSafeReleaseNULL(this->dataQueueMemory);
    OSSafeReleaseNULL(this->dataQueue);
}

void PrjFSLogUserClient::free()
{
    this->cleanUp();
    this->super::free();
}

IOReturn PrjFSLogUserClient::clientClose()
{
    KextLog_DeregisterUserClient(this);
    this->terminate(0);
    return kIOReturnSuccess;
}

IOReturn PrjFSLogUserClient::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
    if (LogMemoryType_MessageQueue == type)
    {
        IOMemoryDescriptor* queueMemory;
        
        Mutex_Acquire(this->dataQueueWriterMutex);
        {
            queueMemory = this->dataQueueMemory;
            if (queueMemory != nullptr)
            {
                queueMemory->retain();
            }
        }
        Mutex_Release(this->dataQueueWriterMutex);
        
        *memory = queueMemory;
        return nullptr == queueMemory ? kIOReturnError : kIOReturnSuccess;
    }
    
    return this->super::clientMemoryForType(type, options, memory);
}

IOReturn PrjFSLogUserClient::registerNotificationPort(mach_port_t port, UInt32 type, io_user_reference_t refCon)
{
    if (type == LogPortType_MessageQueue)
    {
        if (port == MACH_PORT_NULL)
        {
            return kIOReturnError;
        }

        Mutex_Acquire(this->dataQueueWriterMutex);
        {
            assert(nullptr != this->dataQueue);
            this->dataQueue->setNotificationPort(port);
        }
        Mutex_Release(this->dataQueueWriterMutex);

        return kIOReturnSuccess;
    }
    else
    {
        return this->super::registerNotificationPort(port, type, refCon);
    }
}

void PrjFSLogUserClient::sendLogMessage(KextLog_MessageHeader* message, uint32_t size)
{
    Mutex_Acquire(this->dataQueueWriterMutex);
    {
        if (this->logMessageDropped)
        {
            this->logMessageDropped = false;
            message->flags |= LogMessageFlag_LogMessagesDropped;
        }
 
        bool ok = this->dataQueue->enqueue(message, size);
        if (!ok)
        {
            this->logMessageDropped = true;
        }
    }
    Mutex_Release(this->dataQueueWriterMutex);
}

IOReturn PrjFSLogUserClient::externalMethod(
    uint32_t selector,
    IOExternalMethodArguments* arguments,
    IOExternalMethodDispatch* dispatch,
    OSObject* target,
    void* reference)
{
    IOExternalMethodDispatch local_dispatch = {};
    if (selector < sizeof(LogUserClientDispatch) / sizeof(LogUserClientDispatch[0]))
    {
        if (nullptr != LogUserClientDispatch[selector].function)
        {
            local_dispatch = LogUserClientDispatch[selector];
            dispatch = &local_dispatch;
            target = this;
        }
    }
    return this->super::externalMethod(selector, arguments, dispatch, target, reference);
}

IOReturn PrjFSLogUserClient::fetchProfilingData(
    OSObject* target,
    void* reference,
    IOExternalMethodArguments* arguments)
{
    return PerfTracing_ExportDataUserClient(arguments);
}

IOReturn PrjFSLogUserClient::fetchHealthData(
        OSObject* target,
        void* reference,
        IOExternalMethodArguments* arguments)
{
    return VnodeCache_ExportHealthData(arguments);
}

