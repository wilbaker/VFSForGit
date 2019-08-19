#include <kern/debug.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <kern/assert.h>
#include <libkern/version.h>
#include <libkern/libkern.h>
#include <kern/thread.h>

#include "KauthHandlerPrivate.hpp"
#include "public/PrjFSCommon.h"
#include "public/PrjFSPerfCounter.h"
#include "public/PrjFSXattrs.h"
#include "VirtualizationRoots.hpp"
#include "VnodeUtilities.hpp"
#include "KauthHandler.hpp"
#include "public/Message.h"
#include "Locks.hpp"
#include "PrjFSProviderUserClient.hpp"
#include "PerformanceTracing.hpp"
#include "kernel-header-wrappers/mount.h"
#include "kernel-header-wrappers/stdatomic.h"
#include "KextLog.hpp"
#include "ProviderMessaging.hpp"
#include "VnodeCache.hpp"
#include "Memory.hpp"
#include "ArrayUtilities.hpp"

#ifdef KEXT_UNIT_TESTING
#include "KauthHandlerTestable.hpp"
#endif

enum ProviderCallbackPolicy
{
    CallbackPolicy_AllowAny,
    CallbackPolicy_UserInitiatedOnly,
};

enum ProviderStatus
{
    Provider_StatusUnknown,
    Provider_IsOffline,
    Provider_IsOnline,
};

struct PendingRenameOperation
{
    vnode_t vnode;
    thread_t thread;
};

class NullTracer
{
    NullTracer() = delete;
    NullTracer(const NullTracer&) = delete;
    NullTracer& operator=(const NullTracer&) = delete;
public:
    explicit NullTracer(kauth_action_t vnodeAction, vnode_t vnode)
    {}
    explicit NullTracer(kauth_action_t fileopAction, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2)
    {}
    
    void Flush()
    {
    }
    void Printf(const char* format, ...) __attribute__ ((format (printf, 2, 3)))
    {
    }
    void SendProviderMessage(MessageType providerMessage)
    {}
    void SendProviderMessageResult(bool success)
    {}
    
    void SetDeniedForCrawler()
    {}

    void SetVnodeOpResult(int result)
    {
    }
};

static const char* MessageTypeString(MessageType messageType)
{
    switch(messageType)
    {
    case MessageType_KtoU_EnumerateDirectory:            return "EnumerateDirectory";
    case MessageType_KtoU_RecursivelyEnumerateDirectory: return "RecursivelyEnumerateDirectory";
    case MessageType_KtoU_HydrateFile:                   return "HydrateFile";
    case MessageType_KtoU_NotifyFileModified:            return "NotifyFileModified";
    case MessageType_KtoU_NotifyFilePreDelete:           return "NotifyFilePreDelete";
    case MessageType_KtoU_NotifyFilePreDeleteFromRename: return "NotifyFilePreDeleteFromRename";
    case MessageType_KtoU_NotifyDirectoryPreDelete:      return "NotifyDirectoryPreDelete";
    case MessageType_KtoU_NotifyFileCreated:             return "NotifyFileCreated";
    case MessageType_KtoU_NotifyFileRenamed:             return "NotifyFileRenamed";
    case MessageType_KtoU_NotifyDirectoryRenamed:        return "NotifyDirectoryRenamed";
    case MessageType_KtoU_NotifyFileHardLinkCreated:     return "NotifyFileHardLinkCreated";
    case MessageType_KtoU_NotifyFilePreConvertToFull:    return "NotifyFilePreConvertToFull";
    default:                                             return "Unknown";
    };
}

class KextLogTracer
{
    bool discarded;
    bool willEmitTrace;
    bool hasTraceIndex;
    char embeddedTraceBuffer[512];
    char* dynamicTraceBuffer;
    uint32_t dynamicTraceBufferSize;
    uint32_t traceBufferPosition;
    uint64_t traceIndex;

public:
    static RWLock traceFilterLock;
    static _Atomic(char*) pathPrefixFilter;
    static _Atomic(kauth_action_t) traceVnodeActionFilterMask;
    static _Atomic(bool) traceDeniedVnodeEvents;
    static _Atomic(bool) traceProviderMessagingEvents;
    static _Atomic(bool) traceAllFileOpEvents;
    static _Atomic(bool) traceAllVnodeEvents;
    static _Atomic(uint64_t) nextTraceIndex;
private:

    KextLogTracer() = delete;
    KextLogTracer(const NullTracer&) = delete;
    KextLogTracer& operator=(const NullTracer&) = delete;

    struct TraceBuffer
    {
        char* buffer;
        uint32_t size, position;
    };
    
    static bool ShouldTraceEventsForVnode(vnode_t vnode, char (&pathBuffer)[PATH_MAX])
    {
        bool shouldTrace = true;
        if (KextLogTracer::pathPrefixFilter != nullptr)
        {
            int pathLen = sizeof(pathBuffer);
            errno_t error = vn_getpath(vnode, pathBuffer, &pathLen);
            if (error == 0)
            {
                RWLock_AcquireShared(KextLogTracer::traceFilterLock);
                {
                    if (KextLogTracer::pathPrefixFilter != nullptr)
                    {
                        shouldTrace = strprefix(pathBuffer, KextLogTracer::pathPrefixFilter);
                    }
                }
                RWLock_ReleaseShared(KextLogTracer::traceFilterLock);
            }
            else
            {
                snprintf(pathBuffer, sizeof(pathBuffer), "[vn_getpath() failed: %d]", error);
            }
        }
        
        return shouldTrace;
    }
    
    uint64_t GetTraceIndex()
    {
        if (!this->hasTraceIndex)
        {
            this->traceIndex = atomic_fetch_add(&KextLogTracer::nextTraceIndex, 1llu);
            this->hasTraceIndex = true;
        }
        return this->traceIndex;
    }
    
    TraceBuffer GetBuffer()
    {
        if (this->dynamicTraceBuffer != nullptr)
        {
            return TraceBuffer { this->dynamicTraceBuffer, this->dynamicTraceBufferSize, this->traceBufferPosition };
        }
        else
        {
            return TraceBuffer { this->embeddedTraceBuffer, sizeof(this->embeddedTraceBuffer), this->traceBufferPosition };
        }
    }
    
    bool GrowBuffer(uint32_t minimumSize)
    {
        TraceBuffer currentBuffer = this->GetBuffer();
        if (minimumSize <= currentBuffer.size)
        {
            return true;
        }
        
        uint32_t newSize = MAX(minimumSize, currentBuffer.size * 2u);
        char* newBuffer = static_cast<char*>(Memory_Alloc(newSize));
        if (newBuffer == nullptr)
        {
            return false;
        }
        
        memcpy(newBuffer, currentBuffer.buffer, currentBuffer.position);
        newBuffer[currentBuffer.position] = '\0';
        
        if (this->dynamicTraceBuffer != nullptr)
        {
            Memory_Free(this->dynamicTraceBuffer, this->dynamicTraceBufferSize);
        }
        this->dynamicTraceBuffer = newBuffer;
        this->dynamicTraceBufferSize = newSize;
        
        return true;
    }

    void Emit()
    {
        if (!this->discarded)
        {
            this->willEmitTrace = true;

            TraceBuffer currentBuffer = this->GetBuffer();
            uint64_t index = this->GetTraceIndex();
            KextLog("Event trace %9llu: %.*s", index, currentBuffer.position, currentBuffer.buffer);
        }
    }

    void Flush()
    {
        if (!this->discarded)
        {
            this->Emit();
            TraceBuffer currentBuffer = this->GetBuffer();
            currentBuffer.buffer[0] = '\0';
            this->traceBufferPosition = 0;
        }
    };
public:
    KextLogTracer(kauth_action_t vnodeAction, vnode_t vnode) :
        traceBufferPosition(0),
        dynamicTraceBuffer(nullptr),
        dynamicTraceBufferSize(0),
        discarded(false),
        willEmitTrace(false)
    {
        this->embeddedTraceBuffer[0] = '\0';
        
        kauth_action_t mask = atomic_load(&KextLogTracer::traceVnodeActionFilterMask);
        if (0 == (mask & vnodeAction))
        {
            this->discarded = true;
        }
        else
        {
            char vnodePath[PATH_MAX] = "";
            if (!ShouldTraceEventsForVnode(vnode, vnodePath))
            {
                this->discarded = true;
            }
            else
            {
                pid_t pid = proc_selfpid();
                char processName[MAXCOMLEN + 1] = "";
                proc_selfname(processName, sizeof(processName));
                
                if (vnode_isdir(vnode))
                {
                    this->Printf(
                        "Directory vnode '%s' event by process '%s' (PID = %d) action" KextLog_DirectoryVnodeActionFormat,
                        vnodePath,
                        processName,
                        pid,
                        KextLog_DirectoryVnodeActionArgs(vnodeAction, " "));
                }
                else
                {
                    this->Printf(
                        "File vnode '%s' event by process '%s' (PID = %d) action" KextLog_FileVnodeActionFormat,
                        vnodePath,
                        processName,
                        pid,
                        KextLog_FileVnodeActionArgs(vnodeAction, " "));
                }

                if (this->traceAllVnodeEvents)
                {
                    this->willEmitTrace = true;
                }
            }
        }
    }
    
    void SendProviderMessage(MessageType providerMessage)
    {
        if (!this->discarded)
        {
            if (!this->willEmitTrace && atomic_load(&KextLogTracer::traceProviderMessagingEvents))
            {
                this->willEmitTrace = true;
            }
            this->Printf("\nMessage to provider: %u (%s)", providerMessage, MessageTypeString(providerMessage));
            
            if (this->willEmitTrace)
            {
                this->Emit();
            }
        }
    }

    void SendProviderMessageResult(bool success)
    {
        if (!this->discarded)
        {
            this->Printf(" -> result: %s", success ? "success" : "failed");
        }
    }

    void SetDeniedForCrawler()
    {
        if (!this->willEmitTrace && !this->discarded)
        {
            if (!atomic_load(&KextLogTracer::traceAllVnodeEvents) && atomic_load(&KextLogTracer::traceDeniedVnodeEvents))
            {
                // When tracing only denied events, throw out any denied crawlers as they tend to spam the trace
                this->discarded = true;
            }
        }
    }

    void SetVnodeOpResult(int result)
    {
        if (this->discarded)
        {
            return;
        }
        
        if (!this->willEmitTrace && atomic_load(&KextLogTracer::traceDeniedVnodeEvents))
        {
            if (result == KAUTH_RESULT_DENY)
            {
                this->willEmitTrace = true;
            }
            else
            {
                this->discarded = true;
                return;
            }
        }
        
        this->Printf("\n-> %s",
            result == KAUTH_RESULT_DENY ? "KAUTH_RESULT_DENY" :
            result == KAUTH_RESULT_ALLOW ? "KAUTH_RESULT_ALLOW" :
            result == KAUTH_RESULT_DEFER ? "KAUTH_RESULT_DEFER" :
            "UNKNOWN");
    }

    ~KextLogTracer()
    {
        this->Flush();
        
        if (this->dynamicTraceBuffer != nullptr)
        {
            Memory_Free(this->dynamicTraceBuffer, this->dynamicTraceBufferSize);
            this->dynamicTraceBuffer = nullptr;
        }
    }
    
    template <typename... ARGS>
        void Printf(const char* format, ARGS... args)
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
        if (this->discarded)
        {
            return;
        }
        
        TraceBuffer currentBuffer = this->GetBuffer();
        uint32_t bufferSpace = currentBuffer.size - currentBuffer.position;
        int sizeRequired = snprintf(currentBuffer.buffer + currentBuffer.position, bufferSpace, format, args...);
        if (sizeRequired >= bufferSpace)
        {
            if (!this->GrowBuffer(sizeRequired))
            {
                this->traceBufferPosition = currentBuffer.size - 1;
                return;
            }
            
            currentBuffer = this->GetBuffer();
            bufferSpace = currentBuffer.size - currentBuffer.position;
            sizeRequired = snprintf(currentBuffer.buffer + currentBuffer.position, bufferSpace, format, args...);
            assert(sizeRequired < bufferSpace);
        }
        
        this->traceBufferPosition += sizeRequired;
#pragma clang diagnostic pop
    }
};

RWLock KextLogTracer::traceFilterLock;
_Atomic(char*) KextLogTracer::pathPrefixFilter;
_Atomic(kauth_action_t) KextLogTracer::traceVnodeActionFilterMask;
_Atomic(bool) KextLogTracer::traceDeniedVnodeEvents;
_Atomic(bool) KextLogTracer::traceProviderMessagingEvents;
_Atomic(bool) KextLogTracer::traceAllFileOpEvents;
_Atomic(bool) KextLogTracer::traceAllVnodeEvents;
_Atomic(uint64_t) KextLogTracer::nextTraceIndex;


// Function prototypes
KEXT_STATIC int HandleVnodeOperation(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3);

KEXT_STATIC int HandleFileOpOperation(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3);

static bool TryReadVNodeFileFlags(vnode_t vn, vfs_context_t _Nonnull context, uint32_t* flags);
KEXT_STATIC_INLINE bool FileFlagsBitIsSet(uint32_t fileFlags, uint32_t bit);
KEXT_STATIC_INLINE bool TryGetFileIsFlaggedAsInRoot(vnode_t vnode, vfs_context_t _Nonnull context, bool* flaggedInRoot);
KEXT_STATIC_INLINE bool ActionBitIsSet(kauth_action_t action, kauth_action_t mask);
KEXT_STATIC bool CurrentProcessIsAllowedToHydrate();
KEXT_STATIC bool IsFileSystemCrawler(const char* procname);

static void WaitForListenerCompletion();
KEXT_STATIC bool ShouldIgnoreVnodeType(vtype vnodeType, vnode_t vnode);
static bool VnodeIsEligibleForEventHandling(vnode_t vnode);

KEXT_STATIC bool ShouldHandleVnodeOpEvent(
    // In params:
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    kauth_action_t action,

    // Out params:
    uint32_t* vnodeFileFlags,
    int* pid,
    char procname[MAXCOMLEN + 1],
    int* kauthResult,
    int* kauthError);

static bool TryGetVirtualizationRoot(
    // In params:
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    pid_t pidMakingRequest,
    ProviderCallbackPolicy callbackPolicy,
    bool denyIfOffline,

    // Out params:
    VirtualizationRootHandle* root,
    FsidInode* vnodeFsidInode,
    int* kauthResult,
    int* kauthError,
    ProviderStatus* _Nullable providerStatus = nullptr);

KEXT_STATIC bool ShouldHandleFileOpEvent(
    // In params:
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    const char* path,
    kauth_action_t action,
    bool isDirectory,

    // Out params:
    VirtualizationRootHandle* root,
    int* pid);

KEXT_STATIC bool InitPendingRenames();
KEXT_STATIC void CleanupPendingRenames();
KEXT_STATIC void ResizePendingRenames(uint32_t newMaxPendingRenames);

template <typename EventTracer>
int HandleVnodeOperationImpl(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3);

// State
static kauth_listener_t s_vnodeListener = nullptr;
static kauth_listener_t s_fileopListener = nullptr;

static atomic_int s_numActiveKauthEvents;
static SpinLock s_renameLock;
static PendingRenameOperation* s_pendingRenames = nullptr;
KEXT_STATIC uint32_t s_pendingRenameCount = 0;
KEXT_STATIC uint32_t s_maxPendingRenames = 0;
static bool s_osSupportsRenameDetection = false;

// Public functions
kern_return_t KauthHandler_Init()
{
    if (nullptr != s_vnodeListener)
    {
        goto CleanupAndFail;
    }
    
    if (!ProviderMessaging_Init())
    {
        goto CleanupAndFail;
    }
        
    if (VirtualizationRoots_Init())
    {
        goto CleanupAndFail;
    }
    
    if (VnodeCache_Init())
    {
        goto CleanupAndFail;
    }
    
    if (!InitPendingRenames())
    {
        goto CleanupAndFail;
    }

    KextLogTracer::traceFilterLock = RWLock_Alloc();

    s_vnodeListener = kauth_listen_scope(KAUTH_SCOPE_VNODE, HandleVnodeOperationImpl<NullTracer>, nullptr);
    if (nullptr == s_vnodeListener)
    {
        goto CleanupAndFail;
    }
    
    s_fileopListener = kauth_listen_scope(KAUTH_SCOPE_FILEOP, HandleFileOpOperation, nullptr);
    if (nullptr == s_fileopListener)
    {
        goto CleanupAndFail;
    }
    
    return KERN_SUCCESS;
    
CleanupAndFail:
    KauthHandler_Cleanup();
    return KERN_FAILURE;
}

kern_return_t KauthHandler_Cleanup()
{
    kern_return_t result = KERN_SUCCESS;
    
    // First, stop new listener callback calls
    if (nullptr != s_vnodeListener)
    {
        kauth_unlisten_scope(s_vnodeListener);
        s_vnodeListener = nullptr;
    }
    else
    {
        result = KERN_FAILURE;
    }
    
    if (nullptr != s_fileopListener)
    {
        kauth_unlisten_scope(s_fileopListener);
        s_fileopListener = nullptr;
    }
    else
    {
        result = KERN_FAILURE;
    }

    ProviderMessaging_AbortAllOutstandingEvents();
    
    WaitForListenerCompletion();

    RWLock_FreeMemory(&KextLogTracer::traceFilterLock);

    CleanupPendingRenames();
    
    if (VnodeCache_Cleanup())
    {
        result = KERN_FAILURE;
    }

    if (VirtualizationRoots_Cleanup())
    {
        result = KERN_FAILURE;
    }
    
    ProviderMessaging_Cleanup();
    
    return result;
}

KEXT_STATIC void UseMainForkIfNamedStream(
    // In+out params:
    vnode_t& vnode,
    // Out params:
    bool&    putVnodeWhenDone)
{
    // A lot of our file checks such as attribute tests behave oddly if the vnode
    // refers to a named fork/stream; apply the logic as if the vnode operation was
    // occurring on the file itself. (/path/to/file/..namedfork/rsrc)
    if (vnode_isnamedstream(vnode))
    {
        vnode_t mainFileFork = vnode_getparent(vnode);
        assert(NULLVP != mainFileFork);
        
        vnode = mainFileFork;
        putVnodeWhenDone = true;
    }
    else
    {
        putVnodeWhenDone = false;
    }
}

KEXT_STATIC bool InitPendingRenames()
{
    // Only need to/can track renames on Mojave and newer
    s_osSupportsRenameDetection = (version_major >= PrjFSDarwinMajorVersion::MacOS10_14_Mojave);
    if (s_osSupportsRenameDetection)
    {
        s_renameLock = SpinLock_Alloc();
        s_maxPendingRenames = 8; // Arbitrary choice, should be maximum number of expected concurrent threads performing renames, but array will resize on demand
        s_pendingRenameCount = 0;
        s_pendingRenames = Memory_AllocArray<PendingRenameOperation>(s_maxPendingRenames);
        if (!SpinLock_IsValid(s_renameLock) || s_pendingRenames == nullptr)
        {
            return false;
        }

        Array_DefaultInit(s_pendingRenames, s_maxPendingRenames);
    }
    
    return true;
}

KEXT_STATIC void CleanupPendingRenames()
{
    if (s_osSupportsRenameDetection)
    {
        if (SpinLock_IsValid(s_renameLock))
        {
            SpinLock_FreeMemory(&s_renameLock);
        }

        if (s_pendingRenames != nullptr)
        {
            assert(s_pendingRenameCount == 0);
            Memory_FreeArray(s_pendingRenames, s_maxPendingRenames);
            s_pendingRenames = nullptr;
            s_maxPendingRenames = 0;
        }
    }
}

KEXT_STATIC void ResizePendingRenames(uint32_t newMaxPendingRenames)
{
    PendingRenameOperation* newArray = Memory_AllocArray<PendingRenameOperation>(newMaxPendingRenames);
    assert(newArray != nullptr);
    PendingRenameOperation* arrayToFree = nullptr;
    uint32_t arrayToFreeLength = 0;
    
    SpinLock_Acquire(s_renameLock);
    {
        if (newMaxPendingRenames > s_maxPendingRenames)
        {
            Array_CopyElements(newArray, s_pendingRenames, s_maxPendingRenames);
            Array_DefaultInit(newArray + s_maxPendingRenames, newMaxPendingRenames - s_maxPendingRenames);
            
            arrayToFree = s_pendingRenames;
            arrayToFreeLength = s_maxPendingRenames;
            
            s_pendingRenames = newArray;
            s_maxPendingRenames = newMaxPendingRenames;
        }
        else
        {
            arrayToFree = newArray;
            arrayToFreeLength = newMaxPendingRenames;
        }
    }
    SpinLock_Release(s_renameLock);
    
    if (arrayToFree != nullptr)
    {
        assert(arrayToFreeLength > 0);
        Memory_FreeArray(arrayToFree, arrayToFreeLength);
    }
}

KEXT_STATIC void RecordPendingRenameOperation(vnode_t vnode)
{
    assertf(s_osSupportsRenameDetection, "This function should only be called from the KAUTH_FILEOP_WILL_RENAME handler, which is only supported by Darwin 18 (macOS 10.14 Mojave) and newer (version_major = %u)", version_major);
    thread_t myThread = current_thread();
    
    bool resizeTable;
    do
    {
        resizeTable = false;
        uint32_t resizeTableLength = 0;
        
        SpinLock_Acquire(s_renameLock);
        {
            if (s_pendingRenameCount < s_maxPendingRenames)
            {
                s_pendingRenames[s_pendingRenameCount].thread = myThread;
                s_pendingRenames[s_pendingRenameCount].vnode = vnode;
                ++s_pendingRenameCount;
            }
            else
            {
                for (uint32_t i = 0; i < s_pendingRenameCount; ++i)
                {
                    assert(s_pendingRenames[i].thread != myThread);
                }

                resizeTable = true;
                
                resizeTableLength = static_cast<uint32_t>(clamp(s_maxPendingRenames * UINT64_C(2), 1u, UINT32_MAX));
                assert(resizeTableLength > s_maxPendingRenames);
            }
        }
        SpinLock_Release(s_renameLock);
        
        if (resizeTable)
        {
            if (resizeTableLength > 16)
            {
                KextLog_Error("Warning: RecordPendingRenameOperation is causing pending rename array resize to %u items.", resizeTableLength);
            }
            ResizePendingRenames(resizeTableLength);
        }
    } while (resizeTable);
}

KEXT_STATIC bool DeleteOpIsForRename(vnode_t vnode)
{
    if (!s_osSupportsRenameDetection)
    {
        // High Sierra and earlier do not support WILL_RENAME notification, so we have to assume any delete may be caused by a rename
        assert(s_pendingRenameCount == 0);
        assert(s_maxPendingRenames == 0);
        return true;
    }
    
    bool isRename = false;
    
    thread_t myThread = current_thread();
    
    SpinLock_Acquire(s_renameLock);
    {
        for (uint32_t i = 0; i < s_pendingRenameCount; ++i)
        {
            if (s_pendingRenames[i].thread == myThread)
            {
                isRename = true;
                assert(s_pendingRenames[i].vnode == vnode);
                --s_pendingRenameCount;
                if (i != s_pendingRenameCount)
                {
                    s_pendingRenames[i] = s_pendingRenames[s_pendingRenameCount];
                }
                
                s_pendingRenames[s_pendingRenameCount] = PendingRenameOperation{};
                break;
            }
        }
    }
    SpinLock_Release(s_renameLock);

    return isRename;
}

template <typename EventTracer>
    bool TraceAndTrySendRequestAndWaitForResponse(
        EventTracer& eventTracer,
        VirtualizationRootHandle root,
        MessageType messageType,
        const vnode_t vnode,
        const FsidInode& vnodeFsidInode,
        const char* vnodePath,
        const char* fromPath,
        int pid,
        const char* procname,
        int* kauthResult,
        int* kauthError)
{
    eventTracer.SendProviderMessage(MessageType_KtoU_RecursivelyEnumerateDirectory);
    bool messageSuccess = ProviderMessaging_TrySendRequestAndWaitForResponse(root, messageType, vnode, vnodeFsidInode, vnodePath, fromPath, pid, procname, kauthResult, kauthError);
    eventTracer.SendProviderMessageResult(messageSuccess);
    return messageSuccess;
}

// Private functions
template <typename EventTracer>
int HandleVnodeOperationImpl(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3)
{
    atomic_fetch_add(&s_numActiveKauthEvents, 1);

    PerfTracer perfTracer;
    PerfSample vnodeOpFunctionSample(&perfTracer, PrjFSPerfCounter_VnodeOp);
    
    vfs_context_t _Nonnull context = reinterpret_cast<vfs_context_t>(arg0);
    vnode_t currentVnode =  reinterpret_cast<vnode_t>(arg1);
    // arg2 is the (vnode_t) parent vnode
    int* kauthError =       reinterpret_cast<int*>(arg3);
    int kauthResult = KAUTH_RESULT_DEFER;
    bool putVnodeWhenDone = false;

    EventTracer eventTracer(action, currentVnode);

    UseMainForkIfNamedStream(currentVnode, putVnodeWhenDone);

    VirtualizationRootHandle root = RootHandle_None;
    uint32_t currentVnodeFileFlags;
    FsidInode vnodeFsidInode;
    int pid = 0;
    char procname[MAXCOMLEN + 1] = "";
    bool isDeleteAction = false;
    bool isDirectory = false;
    bool isRename = false;
    
    {
        PerfSample considerVnodeSample(&perfTracer, PrjFSPerfCounter_VnodeOp_BasicVnodeChecks);
        if (!VnodeIsEligibleForEventHandling(currentVnode))
        {
            goto CleanupAndReturn;
        }
    }

    isDeleteAction = ActionBitIsSet(action, KAUTH_VNODE_DELETE);
    if (isDeleteAction)
    {
        // This removes a matching entry from the array, so must run under the same
        // conditions as the original RecordPendingRenameOperation call - hence early
        // in the callback.
        isRename = DeleteOpIsForRename(currentVnode);
    }

    if (!ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            currentVnode,
            action,
            &currentVnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            kauthError))
    {
        if (kauthResult == KAUTH_RESULT_DENY && *kauthError != EBADF)
        {
            eventTracer.SetDeniedForCrawler();
        }
        goto CleanupAndReturn;
    }
    
    isDirectory = vnode_isdir(currentVnode);
    
    if (isDirectory)
    {
        if (isRename ||
            ActionBitIsSet(
                action,
                KAUTH_VNODE_LIST_DIRECTORY |
                KAUTH_VNODE_SEARCH |
                KAUTH_VNODE_READ_SECURITY |
                KAUTH_VNODE_READ_ATTRIBUTES |
                KAUTH_VNODE_READ_EXTATTRIBUTES))
        {
            // Recursively expand directory on rename as user will expect the moved directory to have the same contents as in its original location
            if (isRename)
            {
                if (!TryGetVirtualizationRoot(
                        &perfTracer,
                        context,
                        currentVnode,
                        pid,
                        // Prevent system services from expanding directories as part of enumeration as this tends to cause deadlocks with the kauth listeners for Antivirus software
                        CallbackPolicy_UserInitiatedOnly,
                        // We want to block directory renames when provider is offline, but we can only do this on
                        // newer OS versions as any delete operation could be a rename on High Sierra and older,
                        // so on those versions, isRename is true for all delete events.
                        s_osSupportsRenameDetection,
                        &root,
                        &vnodeFsidInode,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }

                PerfSample recursivelyEnumerateSample(&perfTracer, PrjFSPerfCounter_VnodeOp_RecursivelyEnumerateDirectory);
        
                if (!TraceAndTrySendRequestAndWaitForResponse(
                        eventTracer,
                        root,
                        MessageType_KtoU_RecursivelyEnumerateDirectory,
                        currentVnode,
                        vnodeFsidInode,
                        nullptr, // path not needed, use fsid/inode
                        nullptr, // source path N/A
                        pid,
                        procname,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }
            }
            else if (FileFlagsBitIsSet(currentVnodeFileFlags, FileFlags_IsEmpty))
            {
                if (!TryGetVirtualizationRoot(
                        &perfTracer,
                        context,
                        currentVnode,
                        pid,
                        // Prevent system services from expanding directories as part of enumeration as this tends to cause deadlocks with the kauth listeners for Antivirus software
                        CallbackPolicy_UserInitiatedOnly,
                        false, // allow reading offline directories even if not expanded
                        &root,
                        &vnodeFsidInode,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }

                PerfSample enumerateDirectorySample(&perfTracer, PrjFSPerfCounter_VnodeOp_EnumerateDirectory);
        
                if (!TraceAndTrySendRequestAndWaitForResponse(
                        eventTracer,
                        root,
                        MessageType_KtoU_EnumerateDirectory,
                        currentVnode,
                        vnodeFsidInode,
                        nullptr, // path not needed, use fsid/inode
                        nullptr, // source path N/A
                        pid,
                        procname,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }
            }
        }
        else if (ActionBitIsSet(action, KAUTH_VNODE_ADD_FILE | KAUTH_VNODE_ADD_SUBDIRECTORY))
        {
            if (!TryGetVirtualizationRoot(
                    &perfTracer,
                    context,
                    currentVnode,
                    pid,
                    CallbackPolicy_AllowAny,
                    // Block creating new files in offline roots.
                    true,
                    &root,
                    &vnodeFsidInode,
                    &kauthResult,
                    kauthError))
            {
                goto CleanupAndReturn;
            }
        }
    }
    else
    {
        if (isRename || // Hydrate before a file is moved as the user will not expect an empty file at the new location
            ActionBitIsSet(
                action,
                KAUTH_VNODE_READ_ATTRIBUTES |
                KAUTH_VNODE_WRITE_ATTRIBUTES |
                KAUTH_VNODE_READ_EXTATTRIBUTES |
                KAUTH_VNODE_WRITE_EXTATTRIBUTES |
                KAUTH_VNODE_READ_DATA |
                KAUTH_VNODE_WRITE_DATA |
                KAUTH_VNODE_EXECUTE |
                KAUTH_VNODE_APPEND_DATA))
        {
            if (FileFlagsBitIsSet(currentVnodeFileFlags, FileFlags_IsEmpty))
            {
                // Prevent access to empty files in offline roots, except always allow the user to delete files.
                bool shouldBlockIfOffline =
                    (isRename && s_osSupportsRenameDetection) ||
                    ActionBitIsSet(
                        action,
                        // Writes would get overwritten by subsequent hydration
                        KAUTH_VNODE_WRITE_ATTRIBUTES |
                        KAUTH_VNODE_WRITE_EXTATTRIBUTES |
                        KAUTH_VNODE_WRITE_DATA |
                        KAUTH_VNODE_APPEND_DATA |
                        // Reads would yield bad (null) data
                        KAUTH_VNODE_READ_DATA |
                        KAUTH_VNODE_READ_ATTRIBUTES |
                        KAUTH_VNODE_EXECUTE |
                        KAUTH_VNODE_READ_EXTATTRIBUTES);
                if (!TryGetVirtualizationRoot(
                        &perfTracer,
                        context,
                        currentVnode,
                        pid,
                        // Prevent system services from hydrating files as this tends to cause deadlocks with the kauth listeners for Antivirus software
                        CallbackPolicy_UserInitiatedOnly,
                        shouldBlockIfOffline,
                        &root,
                        &vnodeFsidInode,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }

                PerfSample enumerateDirectorySample(&perfTracer, PrjFSPerfCounter_VnodeOp_HydrateFile);

                if (!TraceAndTrySendRequestAndWaitForResponse(
                        eventTracer,
                        root,
                        MessageType_KtoU_HydrateFile,
                        currentVnode,
                        vnodeFsidInode,
                        nullptr, // path not needed, use fsid/inode
                        nullptr, // source path N/A
                        pid,
                        procname,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }
            }
            
            if (ActionBitIsSet(action, KAUTH_VNODE_WRITE_DATA | KAUTH_VNODE_APPEND_DATA))
            {
                /* At this stage we know the file is NOT empty, but it could be
                 * in either placeholder (hydrated) or full (modified/added to
                 * watchlist) state.
                 *
                 * If it's a placeholder, we need to notify the provider before
                 * allowing modifications so that it is tracked as a full file
                 * (and the placeholder xattr is removed).
                 * If the provider is offline in this situation, we want to
                 * prevent writes (by most processes) to the file so that the
                 * modifications don't go undetected.
                 *
                 * If it's already a full file on the other hand, nothing needs
                 * to be done, so we can allow writes even when the provider is
                 * offline.
                 */
                ProviderStatus providerStatus;
                bool shouldMessageProvider = TryGetVirtualizationRoot(
                    &perfTracer,
                    context,
                    currentVnode,
                    pid,
                    CallbackPolicy_UserInitiatedOnly,
                    // At this stage, we don't yet know if the file is still a placeholder, so don't deny yet even if offline
                    false, // denyIfOffline
                    &root,
                    &vnodeFsidInode,
                    &kauthResult,
                    kauthError,
                    &providerStatus);
                
                if (!shouldMessageProvider)
                {
                    // If we don't need to message the provider, that's normally
                    // the end of it, but in the case where the provider is
                    // offline we need to do further checks.
                    if (providerStatus != Provider_IsOffline)
                    {
                        goto CleanupAndReturn;
                    }
                }
                
                PrjFSFileXAttrData rootXattr = {};
                SizeOrError xattrResult = Vnode_ReadXattr(currentVnode, PrjFSFileXAttrName, &rootXattr, sizeof(rootXattr));
                if (xattrResult.error == ENOATTR)
                {
                    // If the file does not have the attribute, this means it's
                    // already in the "full" state, so no need to send a provider
                    // message or block the I/O if the provider is offline
                    goto CleanupAndReturn;
                }
                
                if (providerStatus == Provider_IsOffline)
                {
                    assert(!shouldMessageProvider);
                    // Deny write access to placeholders in an offline root
                    kauthResult = KAUTH_RESULT_DENY;
                    goto CleanupAndReturn;
                }
                
                assert(shouldMessageProvider);
                assert(providerStatus == Provider_IsOnline);

                PerfSample preConvertToFullSample(&perfTracer, PrjFSPerfCounter_VnodeOp_PreConvertToFull);
                
                if (!TraceAndTrySendRequestAndWaitForResponse(
                        eventTracer,
                        root,
                        MessageType_KtoU_NotifyFilePreConvertToFull,
                        currentVnode,
                        vnodeFsidInode,
                        nullptr, // path not needed, use fsid/inode,
                        nullptr, // source path N/A
                        pid,
                        procname,
                        &kauthResult,
                        kauthError))
                {
                    goto CleanupAndReturn;
                }
            }
        }
    }
    
    if (isDeleteAction)
    {
        if (!TryGetVirtualizationRoot(
                &perfTracer,
                context,
                currentVnode,
                pid,
                // Allow any user to delete individual files, as this generally doesn't cause nested kauth callbacks.
                CallbackPolicy_AllowAny,
                // Allow deletes even if provider offline, except renames. Allow all deletes
                // on OS versions where we can't distinguish renames & other deletes.
                isRename && s_osSupportsRenameDetection,
                &root,
                &vnodeFsidInode,
                &kauthResult,
                kauthError))
        {
            goto CleanupAndReturn;
        }
                
        PerfSample preDeleteSample(&perfTracer, PrjFSPerfCounter_VnodeOp_PreDelete);
        
        // Predeletes must be sent after hydration since they may convert the file to full
        if (!TraceAndTrySendRequestAndWaitForResponse(
                eventTracer,
                root,
                isDirectory ?
                MessageType_KtoU_NotifyDirectoryPreDelete :
                isRename ? MessageType_KtoU_NotifyFilePreDeleteFromRename : MessageType_KtoU_NotifyFilePreDelete,
                currentVnode,
                vnodeFsidInode,
                nullptr, // path not needed, use fsid/inode
                nullptr, // source path N/A
                pid,
                procname,
                &kauthResult,
                kauthError))
        {
            goto CleanupAndReturn;
        }
    }

    
CleanupAndReturn:
    eventTracer.SetVnodeOpResult(kauthResult);

    if (putVnodeWhenDone)
    {
        vnode_put(currentVnode);
    }
    
    atomic_fetch_sub(&s_numActiveKauthEvents, 1);
    return kauthResult;
}

#ifdef KEXT_UNIT_TESTING
KEXT_STATIC int HandleVnodeOperation(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3)
{
    return HandleVnodeOperationImpl<NullTracer>(credential, idata, action, arg0, arg1, arg2, arg3);
}
#endif

// Note: a fileop listener MUST NOT return an error, or it will result in a kernel panic.
// Fileop events are informational only.
KEXT_STATIC int HandleFileOpOperation(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3)
{
    atomic_fetch_add(&s_numActiveKauthEvents, 1);
    
    PerfTracer perfTracer;
    PerfSample fileOpSample(&perfTracer, PrjFSPerfCounter_FileOp);

    vfs_context_t _Nonnull context = vfs_context_create(NULL);
    vnode_t currentVnode = NULLVP;
    bool    putCurrentVnode = false;

    if (KAUTH_FILEOP_RENAME == action)
    {
        // arg0 is the (const char *) fromPath (or the file being linked to)
        const char* newPath = reinterpret_cast<const char*>(arg1);
        
        // TODO(#1367): We need to handle failures to lookup the vnode.  If we fail to lookup the vnode
        // it's possible that we'll miss notifications
        errno_t toErr = vnode_lookup(newPath, 0 /* flags */, &currentVnode, context);
        if (0 != toErr)
        {
            KextLog_Error("HandleFileOpOperation (KAUTH_FILEOP_RENAME): vnode_lookup failed, errno %d for path '%s'", toErr, newPath);
            goto CleanupAndReturn;
        }
        
        // Don't expect named stream here as they can't be directly hardlinked or renamed, only the main fork can
        assert(!vnode_isnamedstream(currentVnode));
        
        putCurrentVnode = true;
        
        bool isDirectory = (0 != vnode_isdir(currentVnode));
        
        VirtualizationRootHandle root = RootHandle_None;
        int pid;
        if (!ShouldHandleFileOpEvent(
                &perfTracer,
                context,
                currentVnode,
                nullptr, // use vnode for lookup, not path
                action,
                isDirectory,
                &root,
                &pid))
        {
            goto CleanupAndReturn;
        }
        
        FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(currentVnode, context, true /* the inode is used for getting the path in the provider, so use linkid */);

        char procname[MAXCOMLEN + 1];
        proc_name(pid, procname, MAXCOMLEN + 1);

        {
            PerfSample renameSample(&perfTracer, PrjFSPerfCounter_FileOp_Renamed);
            
            MessageType messageType =
                isDirectory
                ? MessageType_KtoU_NotifyDirectoryRenamed
                : MessageType_KtoU_NotifyFileRenamed;

            int kauthResult;
            int kauthError;
            if (!ProviderMessaging_TrySendRequestAndWaitForResponse(
                    root,
                    messageType,
                    currentVnode,
                    vnodeFsidInode,
                    newPath,
                    nullptr, // fromPath
                    pid,
                    procname,
                    &kauthResult,
                    &kauthError))
            {
                goto CleanupAndReturn;
            }
        }
    }
    else if (KAUTH_FILEOP_LINK == action)
    {
        const char* fromPath = reinterpret_cast<const char*>(arg0);
        const char* newPath = reinterpret_cast<const char*>(arg1);
        
        // TODO(#1367): We need to handle failures to lookup the vnode.  If we fail to lookup the vnode
        // it's possible that we'll miss notifications
        errno_t toErr = vnode_lookup(newPath, 0 /* flags */, &currentVnode, context);
        if (0 != toErr)
        {
            KextLog_Error("HandleFileOpOperation (KAUTH_FILEOP_LINK): vnode_lookup failed, errno %d for path '%s'", toErr, newPath);
            goto CleanupAndReturn;
        }
        
        // Don't expect named stream here as they can't be directly hardlinked or renamed, only the main fork can
        assert(!vnode_isnamedstream(currentVnode));
        
        putCurrentVnode = true;
        
        bool isDirectory = (0 != vnode_isdir(currentVnode));
        if (isDirectory)
        {
            KextLog_Info("HandleFileOpOperation: KAUTH_FILEOP_LINK event for hardlinked directory currently not handled. ('%s' -> '%s')", fromPath, newPath);
            goto CleanupAndReturn;
        }
        
        VirtualizationRootHandle targetRoot, fromRoot;
        pid_t pid;
        bool messageTargetProvider =
            ShouldHandleFileOpEvent(
                &perfTracer,
                context,
                currentVnode,
                nullptr, // don't pass path for target provider
                action,
                isDirectory,
                &targetRoot,
                &pid);
        bool messageFromProvider =
            ShouldHandleFileOpEvent(
                &perfTracer,
                context,
                currentVnode,
                fromPath,
                action,
                isDirectory,
                &fromRoot,
                &pid);
        
        if (!messageTargetProvider && !messageFromProvider)
        {
            goto CleanupAndReturn;
        }
        
        FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(currentVnode, context, true /* the inode is used for getting the path in the provider, so use linkid */);

        char procname[MAXCOMLEN + 1];
        proc_name(pid, procname, MAXCOMLEN + 1);

        {
            PerfSample sample(&perfTracer, PrjFSPerfCounter_FileOp_HardLinkCreated);

            if (messageTargetProvider)
            {
                int kauthResult;
                int kauthError = 0;
                if (!ProviderMessaging_TrySendRequestAndWaitForResponse(
                        targetRoot,
                        MessageType_KtoU_NotifyFileHardLinkCreated,
                        currentVnode,
                        vnodeFsidInode,
                        newPath,
                        (messageFromProvider && targetRoot == fromRoot) ? fromPath : "", // Send "", to specify that the fromPath is not in the same root
                        pid,
                        procname,
                        &kauthResult,
                        &kauthError))
                {
                    KextLog_Error("HandleFileOpOperation: Request NotifyFileHardLinkCreated to destination provider %d failed, kauthResult = %u, kauthError = %u",
                        targetRoot, kauthResult, kauthError);
                }
            }
            
            if (messageFromProvider && (!messageTargetProvider || targetRoot != fromRoot)) // Don't send the same message to the same provider twice
            {
                int kauthResult;
                int kauthError = 0;
                if (!ProviderMessaging_TrySendRequestAndWaitForResponse(
                        fromRoot,
                        MessageType_KtoU_NotifyFileHardLinkCreated,
                        // vnode & target path are not in "fromRoot", so don't send them
                        nullptr, // vnode
                        FsidInode{},
                        "", // Send target path as "" to signal the path is outside the Virtualization Root 
                        fromPath,
                        pid,
                        procname,
                        &kauthResult,
                        &kauthError))
                {
                    KextLog_Error("HandleFileOpOperation: Request NotifyFileHardLinkCreated to source provider %d failed, kauthResult = %u, kauthError = %u",
                        fromRoot, kauthResult, kauthError);
                }
            }
        }
    }
    else if (KAUTH_FILEOP_OPEN == action)
    {
        currentVnode = reinterpret_cast<vnode_t>(arg0);
        putCurrentVnode = false;
        const char* path = reinterpret_cast<const char*>(arg1);

        if (vnode_isdir(currentVnode))
        {
            goto CleanupAndReturn;
        }

        UseMainForkIfNamedStream(currentVnode, putCurrentVnode);

        bool fileFlaggedInRoot;
        if (!TryGetFileIsFlaggedAsInRoot(currentVnode, context, &fileFlaggedInRoot))
        {
            KextLog_ErrorVnodeProperties(currentVnode, "KAUTH_FILEOP_OPEN: checking file flags failed. Path = '%s'", path);
            
            goto CleanupAndReturn;
        }
        
        if (fileFlaggedInRoot)
        {
            goto CleanupAndReturn;
        }
        
        VirtualizationRootHandle root = RootHandle_None;
        int pid;
        if (!ShouldHandleFileOpEvent(
                &perfTracer,
                context,
                currentVnode,
                nullptr, // use vnode for lookup, not path
                action,
                false /* isDirectory */,
                &root,
                &pid))
        {
            goto CleanupAndReturn;
        }

        FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(currentVnode, context, true /* the inode is used for getting the path in the provider, so use linkid */);

        char procname[MAXCOMLEN + 1];
        proc_name(pid, procname, MAXCOMLEN + 1);
        PerfSample fileCreatedSample(&perfTracer, PrjFSPerfCounter_FileOp_FileCreated);
        int kauthResult;
        int kauthError;
        if (!ProviderMessaging_TrySendRequestAndWaitForResponse(
                root,
                MessageType_KtoU_NotifyFileCreated,
                currentVnode,
                vnodeFsidInode,
                path,
                nullptr, // fromPath
                pid,
                procname,
                &kauthResult,
                &kauthError))
        {
            goto CleanupAndReturn;
        }
    }
    else if (KAUTH_FILEOP_CLOSE == action)
    {
        currentVnode = reinterpret_cast<vnode_t>(arg0);
        putCurrentVnode = false;
        const char* path = reinterpret_cast<const char*>(arg1);
        int closeFlags = static_cast<int>(arg2);
        
        if (vnode_isdir(currentVnode))
        {
            goto CleanupAndReturn;
        }
        
        UseMainForkIfNamedStream(currentVnode, putCurrentVnode);
        
        if (!FileFlagsBitIsSet(closeFlags, KAUTH_FILEOP_CLOSE_MODIFIED))
        {
            goto CleanupAndReturn;
        }
            
        VirtualizationRootHandle root = RootHandle_None;
        int pid;
        if (!ShouldHandleFileOpEvent(
                &perfTracer,
                context,
                currentVnode,
                nullptr, // use vnode for lookup, not path
                action,
                false /* isDirectory */,
                &root,
                &pid))
        {
            goto CleanupAndReturn;
        }

        FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(currentVnode, context, true /* the inode is used for getting the path in the provider, so use linkid */);

        char procname[MAXCOMLEN + 1];
        proc_name(pid, procname, MAXCOMLEN + 1);
        PerfSample fileModifiedSample(&perfTracer, PrjFSPerfCounter_FileOp_FileModified);
        int kauthResult;
        int kauthError;
        if (!ProviderMessaging_TrySendRequestAndWaitForResponse(
                root,
                MessageType_KtoU_NotifyFileModified,
                currentVnode,
                vnodeFsidInode,
                path,
                nullptr, // fromPath
                pid,
                procname,
                &kauthResult,
                &kauthError))
        {
            goto CleanupAndReturn;
        }
    }
    else if (KAUTH_FILEOP_WILL_RENAME == action)
    {
        currentVnode = reinterpret_cast<vnode_t>(arg0);
        if (VnodeIsEligibleForEventHandling(currentVnode))
        {
            // Records thread/vnode as rename() operation in progress, enabling optimisation in subsequent DELETE vnode listener handler
            RecordPendingRenameOperation(currentVnode);
        }
    }
    
CleanupAndReturn:    
    if (NULLVP != currentVnode && putCurrentVnode)
    {
        vnode_put(currentVnode);
    }
    
    vfs_context_rele(context);
    atomic_fetch_sub(&s_numActiveKauthEvents, 1);
    
    // We must always return DEFER from a fileop listener. The kernel does not allow any other
    // result and will panic if we return anything else.
    return KAUTH_RESULT_DEFER;
}

static bool VnodeIsEligibleForEventHandling(vnode_t vnode)
{
    if (!VirtualizationRoot_VnodeIsOnAllowedFilesystem(vnode))
    {
        return false;
    }

    vtype vnodeType = vnode_vtype(vnode);
    if (ShouldIgnoreVnodeType(vnodeType, vnode))
    {
        return false;
    }
    
    return true;
}

KEXT_STATIC bool ShouldHandleVnodeOpEvent(
    // In params:
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    kauth_action_t action,

    // Out params:
    uint32_t* vnodeFileFlags,
    int* pid,
    char procname[MAXCOMLEN + 1],
    int* kauthResult,
    int* kauthError)
{
    PerfSample handleVnodeSample(perfTracer, PrjFSPerfCounter_VnodeOp_ShouldHandle);

    *kauthResult = KAUTH_RESULT_DEFER;
    
    {
        PerfSample isVnodeAccessSample(perfTracer, PrjFSPerfCounter_VnodeOp_ShouldHandle_IsVnodeAccessCheck);
        
        if (ActionBitIsSet(action, KAUTH_VNODE_ACCESS))
        {
            perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_ShouldHandle_IgnoredVnodeAccessCheck);
            
            // From kauth.h:
            //    "The KAUTH_VNODE_ACCESS bit is passed to the callback if the authorisation
            //    request in progress is advisory, rather than authoritative.  Listeners
            //    performing consequential work (i.e. not strictly checking authorisation)
            //    may test this flag to avoid performing unnecessary work."
            *kauthResult = KAUTH_RESULT_DEFER;
            return false;
        }
    }
    
    {
        PerfSample readFlagsSample(perfTracer, PrjFSPerfCounter_VnodeOp_ShouldHandle_ReadFileFlags);
        if (!TryReadVNodeFileFlags(vnode, context, vnodeFileFlags))
        {
            *kauthError = EBADF;
            *kauthResult = KAUTH_RESULT_DENY;
            return false;
        }

        if (!FileFlagsBitIsSet(*vnodeFileFlags, FileFlags_IsInVirtualizationRoot))
        {
            // This vnode is not part of ANY virtualization root, so exit now before doing any more work.
            // This gives us a cheap way to avoid adding overhead to IO outside of a virtualization root.
            
            perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_ShouldHandle_NotInAnyRoot);

            *kauthResult = KAUTH_RESULT_DEFER;
            return false;
        }
    }

    *pid = vfs_context_pid(context);
    proc_name(*pid, procname, MAXCOMLEN + 1);
    
    if (FileFlagsBitIsSet(*vnodeFileFlags, FileFlags_IsEmpty))
    {
        // This vnode is not yet hydrated, so do not allow a file system crawler to force hydration.
        // Once a vnode is hydrated, it's fine to allow crawlers to access those contents.
        
        PerfSample crawlerSample(perfTracer, PrjFSPerfCounter_VnodeOp_ShouldHandle_CheckFileSystemCrawler);
        if (IsFileSystemCrawler(procname))
        {
            // We must DENY file system crawlers rather than DEFER.
            // If we allow the crawler's access to succeed without hydrating, the kauth result will be cached and we won't
            // get called again, so we lose the opportunity to hydrate the file/directory and it will appear as though
            // it is missing its contents.

            perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_ShouldHandle_DeniedFileSystemCrawler);
            
            *kauthResult = KAUTH_RESULT_DENY;
            return false;
        }
    }
    
    return true;
}

static bool TryGetVirtualizationRoot(
    // In params:
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    pid_t pidMakingRequest,
    ProviderCallbackPolicy callbackPolicy,
    bool denyIfOffline,

    // Out params:
    VirtualizationRootHandle* root,
    FsidInode* vnodeFsidInode,
    int* kauthResult,
    int* kauthError,
    ProviderStatus* _Nullable providerStatus)
{
    PerfSample findRootSample(perfTracer, PrjFSPerfCounter_VnodeOp_GetVirtualizationRoot);
    
    if (providerStatus != nullptr)
    {
        *providerStatus = Provider_StatusUnknown;
    }
    
    *root = VnodeCache_FindRootForVnode(
        perfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        vnode,
        context);

    if (RootHandle_ProviderTemporaryDirectory == *root)
    {
        perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_GetVirtualizationRoot_TemporaryDirectory);
    
        *kauthResult = KAUTH_RESULT_DEFER;
        return false;
    }
    else if (RootHandle_None == *root)
    {
        KextLog_File(vnode, "No virtualization root found for file with set flag.");
        perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_GetVirtualizationRoot_NoRootFound);
    
        *kauthResult = KAUTH_RESULT_DEFER;
        return false;
    }
    
    ActiveProviderProperties provider = VirtualizationRoot_GetActiveProvider(*root);
    if (providerStatus != nullptr)
    {
        *providerStatus = provider.isOnline ? Provider_IsOnline : Provider_IsOffline;
    }

    if (!provider.isOnline)
    {
        perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_GetVirtualizationRoot_ProviderOffline);
        
        *kauthResult = KAUTH_RESULT_DEFER;
        if (denyIfOffline && !VirtualizationRoots_ProcessMayAccessOfflineRoots(pidMakingRequest))
        {
            *kauthResult = KAUTH_RESULT_DENY;
        }
        
        return false;
    }
    
    if (provider.pid == pidMakingRequest)
    {
        // If the calling process is the provider, we must exit right away to avoid deadlocks
        perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_GetVirtualizationRoot_OriginatedByProvider);
        
        *kauthResult = KAUTH_RESULT_DEFER;
        return false;
    }
    
    if (callbackPolicy == CallbackPolicy_UserInitiatedOnly && !CurrentProcessIsAllowedToHydrate())
    {
        // Prevent hydration etc. by system services
        KextLog_Info("TryGetVirtualizationRoot: process %d is not allowed to hydrate.", pidMakingRequest);
        perfTracer->IncrementCount(PrjFSPerfCounter_VnodeOp_GetVirtualizationRoot_UserRestriction);
        
        *kauthResult = KAUTH_RESULT_DENY;
        return false;
    }
    
    *vnodeFsidInode = Vnode_GetFsidAndInode(vnode, context, true /* the inode is used for getting the path in the provider, so use linkid */);
    
    return true;
}

KEXT_STATIC bool CurrentProcessIsAllowedToHydrate()
{
    bool nonServiceUser = false;
    
    proc_t process = proc_self();
    
    while (true)
    {
        kauth_cred_t credential = kauth_cred_proc_ref(process);
        uid_t processUID = kauth_cred_getuid(credential);
        kauth_cred_unref(&credential);
        
        if (processUID >= 500)
        {
            nonServiceUser = true;
            break;
        }
        
        pid_t parentPID = proc_ppid(process);
        if (parentPID <= 1)
        {
            break;
        }
        
        proc_t parentProcess = proc_find(parentPID);
        proc_rele(process);
        process = parentProcess;
        if (parentProcess == nullptr)
        {
            KextLog_Error("CurrentProcessIsAllowedToHydrate: Failed to locate ancestor process %d for current process %d\n", parentPID, proc_selfpid());
            break;
        }
    }
    
    
    if (process != nullptr)
    {
        proc_rele(process);
    }
    
    if (!nonServiceUser)
    {
       // When amfid is invoked to check the code signature of a library which has not been hydrated,
       // blocking hydration would cause the launch of an application which depends on the library to fail,
       // so amfid should always be allowed to hydrate.
       char buffer[MAXCOMLEN + 1] = "";
       proc_selfname(buffer, sizeof(buffer));
       if (0 == strcmp(buffer, "amfid"))
       {
          nonServiceUser = true;
       }
    }
    
    return nonServiceUser;
}
    
static VirtualizationRootHandle FindRootForVnodeWithFileOpEvent(
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    kauth_action_t action,
    bool isDirectory)
{
    VirtualizationRootHandle root = RootHandle_None;
    
    if (isDirectory)
    {
        if (KAUTH_FILEOP_RENAME == action)
        {
            // Directory renames into (or out) of virtualization roots require invalidating all of the entries
            // within the directory being renamed (because all of those entires now have a new virtualzation root).
            // Rather than trying to find all vnodes in the cache that are children of the directory being renamed
            // we simply invalidate the entire cache.
            //
            // Potential future optimizations include:
            //   - Only invalidate the cache if the rename moves a directory in or out of a directory
            //   - Keeping a per-root generation ID in the cache to allow invalidating a subset of the cache
            VnodeCache_InvalidateCache(perfTracer);
        }
        
        root = VnodeCache_FindRootForVnode(
            perfTracer,
            PrjFSPerfCounter_FileOp_Vnode_Cache_Hit,
            PrjFSPerfCounter_FileOp_Vnode_Cache_Miss,
            PrjFSPerfCounter_FileOp_FindRoot,
            PrjFSPerfCounter_FileOp_FindRoot_Iteration,
            vnode,
            context);
    }
    else
    {
        // TODO(#1188): Once all hardlink paths are delivered to the appropriate root(s)
        // check if the KAUTH_FILEOP_LINK case can be removed.  For now the check is required to make
        // sure we're looking up the most up-to-date parent information for the vnode on the next
        // access to the vnode cache
        switch(action)
        {
            case KAUTH_FILEOP_LINK:
                root = VnodeCache_InvalidateVnodeRootAndGetLatestRoot(
                    perfTracer,
                    PrjFSPerfCounter_FileOp_Vnode_Cache_Hit,
                    PrjFSPerfCounter_FileOp_Vnode_Cache_Miss,
                    PrjFSPerfCounter_FileOp_FindRoot,
                    PrjFSPerfCounter_FileOp_FindRoot_Iteration,
                    vnode,
                    context);
                break;

            case KAUTH_FILEOP_RENAME:
                root = VnodeCache_RefreshRootForVnode(
                    perfTracer,
                    PrjFSPerfCounter_FileOp_Vnode_Cache_Hit,
                    PrjFSPerfCounter_FileOp_Vnode_Cache_Miss,
                    PrjFSPerfCounter_FileOp_FindRoot,
                    PrjFSPerfCounter_FileOp_FindRoot_Iteration,
                    vnode,
                    context);
                break;

            default:
                root = VnodeCache_FindRootForVnode(
                    perfTracer,
                    PrjFSPerfCounter_FileOp_Vnode_Cache_Hit,
                    PrjFSPerfCounter_FileOp_Vnode_Cache_Miss,
                    PrjFSPerfCounter_FileOp_FindRoot,
                    PrjFSPerfCounter_FileOp_FindRoot_Iteration,
                    vnode,
                    context);
                break;
        }
    }
    
    return root;
}

KEXT_STATIC bool ShouldHandleFileOpEvent(
    // In params:
    PerfTracer* perfTracer,
    vfs_context_t _Nonnull context,
    const vnode_t vnode,
    const char* _Nullable path, // if non-null, path is used for finding provider, not vnode
    kauth_action_t action,
    bool isDirectory,

    // Out params:
    VirtualizationRootHandle* root,
    int* pid)
{
    PerfSample fileOpSample(perfTracer, PrjFSPerfCounter_FileOp_ShouldHandle);

    *root = RootHandle_None;
    
    if (!VnodeIsEligibleForEventHandling(vnode))
    {
        return false;
    }
    
    if (path != nullptr)
    {
        PerfSample findRootSample(perfTracer, PrjFSPerfCounter_FileOp_ShouldHandle_FindProviderPathBased);

        *root = ActiveProvider_FindForPath(path);
        if (!VirtualizationRoot_IsValidRootHandle(*root))
        {
            perfTracer->IncrementCount(PrjFSPerfCounter_FileOp_ShouldHandle_NoProviderFound);
            return false;
        }
    }
    else
    {
        PerfSample findRootSample(perfTracer, PrjFSPerfCounter_FileOp_ShouldHandle_FindVirtualizationRoot);

        *root = FindRootForVnodeWithFileOpEvent(perfTracer, context, vnode, action, isDirectory);
        
        if (!VirtualizationRoot_IsValidRootHandle(*root))
        {
            perfTracer->IncrementCount(PrjFSPerfCounter_FileOp_ShouldHandle_NoRootFound);
            return false;
        }
    }
    
    {
        PerfSample checkRootOnlineSample(perfTracer, PrjFSPerfCounter_FileOp_ShouldHandle_CheckProvider);
        
        ActiveProviderProperties provider = VirtualizationRoot_GetActiveProvider(*root);
        
        if (!provider.isOnline)
        {
            perfTracer->IncrementCount(PrjFSPerfCounter_FileOp_ShouldHandle_OfflineRoot);
            return false;
        }
    
        // If the calling process is the provider, we must exit right away to avoid deadlocks
        *pid = vfs_context_pid(context);
        if (*pid == provider.pid)
        {
            perfTracer->IncrementCount(PrjFSPerfCounter_FileOp_ShouldHandle_OriginatedByProvider);
            return false;
        }
    }

    return true;
}

static void WaitForListenerCompletion()
{
    // Wait until all kauth events have noticed and returned.
    // Always sleeping at least once reduces the likelihood of a race condition
    // between kauth_unlisten_scope and the s_numActiveKauthEvents increment at
    // the start of the callback.
    // This race condition and the inability to work around it is a longstanding
    // bug in the xnu kernel - see comment block in RemoveListener() function of
    // the KauthORama sample code:
    // https://developer.apple.com/library/archive/samplecode/KauthORama/Listings/KauthORama_c.html#//apple_ref/doc/uid/DTS10003633-KauthORama_c-DontLinkElementID_3
    do
    {
        Mutex_Sleep(1, nullptr, nullptr);
    } while (atomic_load(&s_numActiveKauthEvents) > 0);
}


static errno_t GetVNodeAttributes(vnode_t vn, vfs_context_t _Nonnull context, struct vnode_attr* attrs)
{
    VATTR_INIT(attrs);
    VATTR_WANTED(attrs, va_flags);
    
    return vnode_getattr(vn, attrs, context);
}

static bool TryReadVNodeFileFlags(vnode_t vn, vfs_context_t _Nonnull context, uint32_t* flags)
{
    struct vnode_attr attributes = {};
    *flags = 0;
    errno_t err = GetVNodeAttributes(vn, context, &attributes);
    if (0 != err)
    {
        // May fail on some file system types? Perhaps we should early-out depending on mount point anyway.
        // If we see failures we should also consider:
        //   - Falling back on vnode lookup (or custom cache) to determine if file is in the root
        //   - Assuming files are empty if we can't read the flags
        KextLog_FileError(vn, "ReadVNodeFileFlags: GetVNodeAttributes failed with error %d; vnode type: %d, recycled: %s", err, vnode_vtype(vn), vnode_isrecycled(vn) ? "yes" : "no");
        return false;
    }
    
    assert(VATTR_IS_SUPPORTED(&attributes, va_flags));
    *flags = attributes.va_flags;
    return true;
}

KEXT_STATIC_INLINE bool FileFlagsBitIsSet(uint32_t fileFlags, uint32_t bit)
{
    // Note: if multiple bits are set in 'bit', this will return true if ANY are set in fileFlags
    return 0 != (fileFlags & bit);
}

KEXT_STATIC_INLINE bool TryGetFileIsFlaggedAsInRoot(vnode_t vnode, vfs_context_t _Nonnull context, bool* flaggedInRoot)
{
    uint32_t vnodeFileFlags;
    *flaggedInRoot = false;
    if (!TryReadVNodeFileFlags(vnode, context, &vnodeFileFlags))
    {
        return false;
    }
    
    *flaggedInRoot = FileFlagsBitIsSet(vnodeFileFlags, FileFlags_IsInVirtualizationRoot);
    return true;
}

KEXT_STATIC_INLINE bool ActionBitIsSet(kauth_action_t action, kauth_action_t mask)
{
    return action & mask;
}

KEXT_STATIC bool IsFileSystemCrawler(const char* procname)
{
    // These process will crawl the file system and force a full hydration
    if (!strcmp(procname, "mds") ||
        !strcmp(procname, "mdworker") ||
        !strcmp(procname, "mds_stores") ||
        !strcmp(procname, "fseventsd") ||
        !strcmp(procname, "Spotlight"))
    {
        return true;
    }
    
    return false;
}

KEXT_STATIC bool ShouldIgnoreVnodeType(vtype vnodeType, vnode_t vnode)
{
    switch (vnodeType)
    {
    case VNON:
    case VBLK:
    case VCHR:
    case VSOCK:
    case VFIFO:
    case VBAD:
        return true;
	case VREG:
    case VDIR:
    case VLNK:
        break;
    case VSTR:
    case VCPLX:
        KextLog_FileInfo(vnode, "vnode with type %s encountered", vnodeType == VSTR ? "VSTR" : "VCPLX");
        break;
    default:
        KextLog_FileInfo(vnode, "vnode with unknown type %d encountered", vnodeType);
    }
    
    return false;
}

bool KauthHandler_EnableTraceListeners(bool tracingEnabled, const KauthHandlerEventTracingSettings& settings)
{
    if (tracingEnabled)
    {
        RWLock_AcquireExclusive(KextLogTracer::traceFilterLock);
        {
            char* newPrefixFilter = nullptr;
            if (settings.pathPrefixFilter != nullptr)
            {
                size_t filterLength = strlen(settings.pathPrefixFilter);
                if (filterLength + 1 <= UINT32_MAX)
                {
                    newPrefixFilter = static_cast<char*>(Memory_Alloc(static_cast<uint32_t>(filterLength + 1)));
                    memcpy(newPrefixFilter, settings.pathPrefixFilter, filterLength + 1);
                }
            }
            
            char* oldPrefixFilter = atomic_exchange(&KextLogTracer::pathPrefixFilter, newPrefixFilter);
            
            if (oldPrefixFilter != nullptr)
            {
                Memory_Free(oldPrefixFilter, static_cast<uint32_t>(strlen(oldPrefixFilter) + 1));
            }
            atomic_store(&KextLogTracer::traceVnodeActionFilterMask, settings.vnodeActionFilterMask);
            atomic_store(&KextLogTracer::traceDeniedVnodeEvents, settings.traceDeniedVnodeEvents);
            atomic_store(&KextLogTracer::traceProviderMessagingEvents, settings.traceProviderMessagingVnodeEvents);
            atomic_store(&KextLogTracer::traceAllVnodeEvents, settings.traceAllVnodeEvents);
        }
        RWLock_ReleaseExclusive(KextLogTracer::traceFilterLock);
    }
    
    kauth_scope_callback_t vnodeListener = tracingEnabled ? HandleVnodeOperationImpl<KextLogTracer> : HandleVnodeOperationImpl<NullTracer>;
    kauth_listener_t newListenerHandle = kauth_listen_scope(KAUTH_SCOPE_VNODE, vnodeListener, nullptr);
    if (newListenerHandle == nullptr)
    {
        return false;
    }
    
    kauth_unlisten_scope(s_vnodeListener);
    s_vnodeListener = newListenerHandle;
    
    KextLog_Info("KauthHandler_EnableTraceListeners: Now running with tracing %s", tracingEnabled ? "ENABLED" : "DISABLED");
    
    return true;
}
