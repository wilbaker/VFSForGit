#include <string.h>
#include "Locks.hpp"
#include "VnodeCache.hpp"
#include "Memory.hpp"
#include "KextLog.hpp"

#ifdef KEXT_UNIT_TESTING
#include "VnodeCacheTestable.hpp"
#endif

KEXT_STATIC_INLINE void InvalidateCache_ExclusiveLocked();
static inline void UpgradeToExclusiveLock(RWLock& lock);
KEXT_STATIC_INLINE uintptr_t HashVnode(vnode_t _Nonnull vnode);

KEXT_STATIC bool TryFindVnodeIndex_SharedLocked(
    vnode_t _Nonnull vnode,
    uintptr_t startingIndex,
    uintptr_t stoppingIndex,
    /* out parameters */
    uintptr_t& cacheIndex);

static void UpdateCacheEntryToLatest_ExclusiveLocked(
    vfs_context_t _Nonnull context,
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    uintptr_t index,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uint32_t vnodeVid);

static bool FindAndUpdateEntryToLatest_ExclusiveLocked(
    vfs_context_t _Nonnull context,
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uintptr_t startingIndex,
    uintptr_t stoppingIndex,
    uint32_t vnodeVid,
    bool invalidateEntry,
    /* out paramaeters */
    VirtualizationRootHandle& rootHandle);

static void ClearCacheAndInsertEntry_ExclusiveLocked(
    vfs_context_t _Nonnull context,
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uintptr_t startingIndex,
    uint32_t vnodeVid,
    /* out paramaeters */
    VirtualizationRootHandle& rootHandle);

struct VnodeCacheEntry
{
    vnode_t vnode;
    uint32_t vid;   // vnode generation number
    VirtualizationRootHandle virtualizationRoot;
};

KEXT_STATIC uint32_t s_entriesCapacity;
KEXT_STATIC VnodeCacheEntry* s_entries;
static RWLock s_entriesLock;
static const uint32_t MinEntriesCapacity = 0x040000; //  4 MB (assuming 16 bytes per VnodeCacheEntry)
static const uint32_t MaxEntriesCapacity = 0x400000; // 64 MB (assuming 16 bytes per VnodeCacheEntry)

kern_return_t VnodeCache_Init()
{
    if (RWLock_IsValid(s_entriesLock))
    {
        return KERN_FAILURE;
    }
    
    s_entriesLock = RWLock_Alloc();
    if (!RWLock_IsValid(s_entriesLock))
    {
        return KERN_FAILURE;
    }

    s_entriesCapacity = MAX(MinEntriesCapacity, desiredvnodes * 2);
    s_entriesCapacity = MIN(MaxEntriesCapacity, s_entriesCapacity);
    
    s_entries = Memory_AllocArray<VnodeCacheEntry>(s_entriesCapacity);
    if (nullptr == s_entries)
    {
        s_entriesCapacity = 0;
        return KERN_RESOURCE_SHORTAGE;
    }
    
    VnodeCache_InvalidateCache(nullptr);
    
    PerfTracing_RecordSample(PrjFSPerfCounter_CacheCapacity, 0, s_entriesCapacity);
    
    return KERN_SUCCESS;
}

void VnodeCache_Cleanup()
{
    if (nullptr != s_entries)
    {
        Memory_FreeArray<VnodeCacheEntry>(s_entries, s_entriesCapacity);
        s_entries = nullptr;
        s_entriesCapacity = 0;
    }
    
    if (RWLock_IsValid(s_entriesLock))
    {
        RWLock_FreeMemory(&s_entriesLock);
    }
}

VirtualizationRootHandle VnodeCache_FindRootForVnode(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheHitCounter,
    PrjFSPerfCounter cacheMissCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vfs_context_t _Nonnull context,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    bool invalidateEntry)
{
    bool lockElevatedToExclusive = false;
    VirtualizationRootHandle rootHandle = RootHandle_None;
    uintptr_t indexFromHash = HashVnode(vnode);
    uint32_t vnodeVid = vnode_vid(vnode);
    
    RWLock_AcquireShared(s_entriesLock);
    {
        uintptr_t cacheIndex;
        if (TryFindVnodeIndex_SharedLocked(vnode, indexFromHash, indexFromHash, /*out*/ cacheIndex))
        {
            if (vnode == s_entries[cacheIndex].vnode)
            {
                if (invalidateEntry || vnodeVid != s_entries[cacheIndex].vid)
                {
                    perfTracer->IncrementCount(cacheMissCounter, true /*ignoreSampling*/);
                
                    UpgradeToExclusiveLock(s_entriesLock);
                    lockElevatedToExclusive = true;
                    
                    UpdateCacheEntryToLatest_ExclusiveLocked(
                        context,
                        perfTracer,
                        cacheMissFallbackFunctionCounter,
                        cacheMissFallbackFunctionInnerLoopCounter,
                        cacheIndex,
                        vnode,
                        vnodeFsidInode,
                        vnodeVid);
                }
                else
                {
                    perfTracer->IncrementCount(cacheHitCounter, true /*ignoreSampling*/);
                }
                
                rootHandle = s_entries[cacheIndex].virtualizationRoot;
            }
            else
            {
                perfTracer->IncrementCount(cacheMissCounter, true /*ignoreSampling*/);
            
                UpgradeToExclusiveLock(s_entriesLock);
                lockElevatedToExclusive = true;
            
                if (!FindAndUpdateEntryToLatest_ExclusiveLocked(
                        context,
                        perfTracer,
                        cacheMissFallbackFunctionCounter,
                        cacheMissFallbackFunctionInnerLoopCounter,
                        vnode,
                        vnodeFsidInode,
                        cacheIndex,    // starting index
                        indexFromHash, // stopping index
                        vnodeVid,
                        invalidateEntry,
                        /* out */ rootHandle))
                {
                    ClearCacheAndInsertEntry_ExclusiveLocked(
                        context,
                        perfTracer,
                        cacheMissFallbackFunctionCounter,
                        cacheMissFallbackFunctionInnerLoopCounter,
                        vnode,
                        vnodeFsidInode,
                        indexFromHash,
                        vnodeVid,
                        /* out */ rootHandle);
                }
            }
        }
        else
        {
            perfTracer->IncrementCount(cacheMissCounter, true /*ignoreSampling*/);
            
            UpgradeToExclusiveLock(s_entriesLock);
            lockElevatedToExclusive = true;
            
            ClearCacheAndInsertEntry_ExclusiveLocked(
                context,
                perfTracer,
                cacheMissFallbackFunctionCounter,
                cacheMissFallbackFunctionInnerLoopCounter,
                vnode,
                vnodeFsidInode,
                indexFromHash,
                vnodeVid,
                /* out */ rootHandle);
        }
    }
    
    if (lockElevatedToExclusive)
    {
        RWLock_ReleaseExclusive(s_entriesLock);
    }
    else
    {
        RWLock_ReleaseShared(s_entriesLock);
    }
    
    return rootHandle;
}

void VnodeCache_InvalidateCache(PerfTracer* _Nullable perfTracer)
{
    if (perfTracer)
    {
        perfTracer->IncrementCount(PrjFSPerfCounter_CacheInvalidateCount, true /*ignoreSampling*/);
    }

    RWLock_AcquireExclusive(s_entriesLock);
    {
        InvalidateCache_ExclusiveLocked();
    }
    RWLock_ReleaseExclusive(s_entriesLock);
}

KEXT_STATIC_INLINE void InvalidateCache_ExclusiveLocked()
{
    memset(s_entries, 0, s_entriesCapacity * sizeof(VnodeCacheEntry));
}

static inline void UpgradeToExclusiveLock(RWLock& lock)
{
    if (!RWLock_AcquireSharedToExclusive(lock))
    {
        RWLock_AcquireExclusive(lock);
    }
}

KEXT_STATIC_INLINE uintptr_t HashVnode(vnode_t _Nonnull vnode)
{
    uintptr_t vnodeAddress = reinterpret_cast<uintptr_t>(vnode);
    return (vnodeAddress >> 3) % s_entriesCapacity;
}

KEXT_STATIC bool TryFindVnodeIndex_SharedLocked(
    vnode_t _Nonnull vnode,
    uintptr_t startingIndex,
    uintptr_t stoppingIndex,
    /* out parameters */
    uintptr_t& cacheIndex)
{
    // Walk from the starting index until we find:
    //    -> The vnode
    //    -> A NULLVP entry
    //    -> The stopping index
    // If we hit the end of the array, continue searching from the start
    cacheIndex = startingIndex;
    while (vnode != s_entries[cacheIndex].vnode)
    {
        if (NULLVP == s_entries[cacheIndex].vnode)
        {
            return true;
        }
    
        cacheIndex = (cacheIndex + 1) % s_entriesCapacity;
        if (cacheIndex == stoppingIndex)
        {
            // Looped through the entire cache and didn't find an empty slot or the vnode
            return false;
        }
    }
    
    return true;
}

static void UpdateCacheEntryToLatest_ExclusiveLocked(
    vfs_context_t _Nonnull context,
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    uintptr_t index,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uint32_t vnodeVid)
{
    s_entries[index].vnode = vnode;
    s_entries[index].vid = vnodeVid;
    s_entries[index].virtualizationRoot = VirtualizationRoot_FindForVnode(
        perfTracer,
        cacheMissFallbackFunctionCounter,
        cacheMissFallbackFunctionInnerLoopCounter,
        vnode,
        vnodeFsidInode);
}

static bool FindAndUpdateEntryToLatest_ExclusiveLocked(
    vfs_context_t _Nonnull context,
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uintptr_t startingIndex,
    uintptr_t stoppingIndex,
    uint32_t vnodeVid,
    bool invalidateEntry,
    /* out paramaeters */
    VirtualizationRootHandle& rootHandle)
{
    uintptr_t insertionIndex;
    if (TryFindVnodeIndex_SharedLocked(
            vnode,
            startingIndex,
            stoppingIndex,
            /*out*/ insertionIndex))
    {
        if (invalidateEntry || NULLVP == s_entries[insertionIndex].vnode || vnodeVid != s_entries[insertionIndex].vid)
        {
            UpdateCacheEntryToLatest_ExclusiveLocked(
                context,
                perfTracer,
                cacheMissFallbackFunctionCounter,
                cacheMissFallbackFunctionInnerLoopCounter,
                insertionIndex,
                vnode,
                vnodeFsidInode,
                vnodeVid);
        }
        
        rootHandle = s_entries[insertionIndex].virtualizationRoot;
        
        return true;
    }
    
    return false;
}

static void ClearCacheAndInsertEntry_ExclusiveLocked(
    vfs_context_t _Nonnull context,
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uintptr_t startingIndex,
    uint32_t vnodeVid,
    /* out paramaeters */
    VirtualizationRootHandle& rootHandle)
{
    perfTracer->IncrementCount(PrjFSPerfCounter_CacheFullCount, true /*ignoreSampling*/);
    InvalidateCache_ExclusiveLocked();

    if (!FindAndUpdateEntryToLatest_ExclusiveLocked(
                context,
                perfTracer,
                cacheMissFallbackFunctionCounter,
                cacheMissFallbackFunctionInnerLoopCounter,
                vnode,
                vnodeFsidInode,
                startingIndex, // starting index
                startingIndex, // stopping index
                vnodeVid,
                true,          // invalidateEntry
                /* out */ rootHandle))
    {
        KextLog_FileError(
            vnode,
            "ClearCacheAndInsertEntry: failed to insert vnode (0x%lu) after emptying cache",
            reinterpret_cast<uintptr_t>(vnode));
        
        rootHandle = VirtualizationRoot_FindForVnode(
            perfTracer,
            cacheMissFallbackFunctionCounter,
            cacheMissFallbackFunctionInnerLoopCounter,
            vnode,
            vnodeFsidInode);
    }
}
