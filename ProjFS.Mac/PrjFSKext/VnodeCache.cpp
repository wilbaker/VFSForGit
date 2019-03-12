#include <string.h>
#include "Locks.hpp"
#include "VnodeCache.hpp"
#include "Memory.hpp"
#include "KextLog.hpp"
#include "../PrjFSKext/public/PrjFSCommon.h"

#include "VnodeCachePrivate.hpp"

#ifdef KEXT_UNIT_TESTING
#include "VnodeCacheTestable.hpp"
#endif

KEXT_STATIC_INLINE void InvalidateCache_ExclusiveLocked();
KEXT_STATIC_INLINE uintptr_t ComputeVnodeHashKey(vnode_t _Nonnull vnode);

KEXT_STATIC bool TryGetVnodeRootFromCache(
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    uint32_t vnodeVid,
    /* out parameters */
    VirtualizationRootHandle& rootHandle);

KEXT_STATIC void UpdateCacheForVnode(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vfs_context_t _Nonnull context,
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    uint32_t vnodeVid,
    bool invalidateEntry,
    /* out parameters */
    VirtualizationRootHandle& rootHandle);

KEXT_STATIC bool TryFindVnodeIndex_Locked(
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    /* out parameters */
    uintptr_t& vnodeIndex);

KEXT_STATIC bool TryInsertOrUpdateEntry_ExclusiveLocked(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    vfs_context_t _Nonnull context,
    uintptr_t vnodeHash,
    uint32_t vnodeVid,
    bool invalidateEntry,
    /* out parameters */
    VirtualizationRootHandle& rootHandle);

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

    s_entriesCapacity = Clamp(desiredvnodes * 2u, MinEntriesCapacity, MaxEntriesCapacity);
    
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

kern_return_t VnodeCache_Cleanup()
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
        return KERN_SUCCESS;
    }
    
    return KERN_FAILURE;
}

VirtualizationRootHandle VnodeCache_FindRootForVnode(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheHitCounter,
    PrjFSPerfCounter cacheMissCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    vfs_context_t _Nonnull context,
    bool invalidateEntry)
{
    VirtualizationRootHandle rootHandle = RootHandle_None;
    uintptr_t vnodeHash = ComputeVnodeHashKey(vnode);
    uint32_t vnodeVid = vnode_vid(vnode);
    
    if (!invalidateEntry)
    {
        if (TryGetVnodeRootFromCache(vnode, vnodeHash, vnodeVid, rootHandle))
        {
            perfTracer->IncrementCount(cacheHitCounter, true /*ignoreSampling*/);
            return rootHandle;
        }
    }
    
    perfTracer->IncrementCount(cacheMissCounter, true /*ignoreSampling*/);
    UpdateCacheForVnode(
        perfTracer,
        cacheMissFallbackFunctionCounter,
        cacheMissFallbackFunctionInnerLoopCounter,
        context,
        vnode,
        vnodeHash,
        vnodeVid,
        invalidateEntry,
        rootHandle);
    
    return rootHandle;
}

void VnodeCache_InvalidateCache(PerfTracer* _Nullable perfTracer)
{
    if (nullptr != perfTracer)
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

KEXT_STATIC_INLINE uintptr_t ComputeVnodeHashKey(vnode_t _Nonnull vnode)
{
    uintptr_t vnodeAddress = reinterpret_cast<uintptr_t>(vnode);
    return (vnodeAddress >> 3) % s_entriesCapacity;
}

KEXT_STATIC bool TryGetVnodeRootFromCache(
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    uint32_t vnodeVid,
    /* out parameters */
    VirtualizationRootHandle& rootHandle)
{
    bool rootFound = false;
    rootHandle = RootHandle_None;

    RWLock_AcquireShared(s_entriesLock);
    {
        uintptr_t vnodeIndex;
        if (TryFindVnodeIndex_Locked(vnode, vnodeHash, /*out*/ vnodeIndex))
        {
            if (vnode == s_entries[vnodeIndex].vnode && vnodeVid == s_entries[vnodeIndex].vid)
            {
                rootFound = true;
                rootHandle = s_entries[vnodeIndex].virtualizationRoot;
            }
        }
    }
    RWLock_ReleaseShared(s_entriesLock);
    
    return rootFound;
}

KEXT_STATIC void UpdateCacheForVnode(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vfs_context_t _Nonnull context,
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    uint32_t vnodeVid,
    bool invalidateEntry,
    /* out parameters */
    VirtualizationRootHandle& rootHandle)
{
    RWLock_AcquireExclusive(s_entriesLock);
    {
        if (!TryInsertOrUpdateEntry_ExclusiveLocked(
                perfTracer,
                cacheMissFallbackFunctionCounter,
                cacheMissFallbackFunctionInnerLoopCounter,
                vnode,
                context,
                vnodeHash,
                vnodeVid,
                invalidateEntry,
                /* out */ rootHandle))
        {
            // TryInsertOrUpdateEntry_ExclusiveLocked can only fail if the cache is full
            
            perfTracer->IncrementCount(PrjFSPerfCounter_CacheFullCount, true /*ignoreSampling*/);
        
            InvalidateCache_ExclusiveLocked();
            if (!TryInsertOrUpdateEntry_ExclusiveLocked(
                        perfTracer,
                        cacheMissFallbackFunctionCounter,
                        cacheMissFallbackFunctionInnerLoopCounter,
                        vnode,
                        context,
                        vnodeHash,
                        vnodeVid,
                        true, // invalidateEntry
                        /* out */ rootHandle))
            {
                KextLog_FileError(
                    vnode,
                    "UpdateCacheForVnode: failed to insert vnode (%p:%u) after emptying cache",
                    KextLog_Unslide(vnode), vnodeVid);
                
                rootHandle = VirtualizationRoot_FindForVnode(
                    perfTracer,
                    cacheMissFallbackFunctionCounter,
                    cacheMissFallbackFunctionInnerLoopCounter,
                    vnode,
                    context);
            }
        }
    }
    RWLock_ReleaseExclusive(s_entriesLock);
}

KEXT_STATIC bool TryFindVnodeIndex_Locked(
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    /* out parameters */
    uintptr_t& vnodeIndex)
{
    // Walk from the starting index until we do one of the following:
    //    -> Find the vnode
    //    -> Find where the vnode should be inserted (i.e. NULLVP)
    //    -> Have looped all the way back to where we started
    vnodeIndex = vnodeHash;
    while (vnode != s_entries[vnodeIndex].vnode && NULLVP != s_entries[vnodeIndex].vnode)
    {
        vnodeIndex = (vnodeIndex + 1) % s_entriesCapacity;
        
        if (vnodeIndex == vnodeHash)
        {
            // Looped through the entire cache and didn't find an empty slot or the vnode
            return false;
        }
    }
    
    return true;
}

KEXT_STATIC bool TryInsertOrUpdateEntry_ExclusiveLocked(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    vnode_t _Nonnull vnode,
    vfs_context_t _Nonnull context,
    uintptr_t vnodeHash,
    uint32_t vnodeVid,
    bool invalidateEntry,
    /* out parameters */
    VirtualizationRootHandle& rootHandle)
{
    uintptr_t vnodeIndex;
    if (TryFindVnodeIndex_Locked(vnode, vnodeHash, /*out*/ vnodeIndex))
    {
        if (invalidateEntry || NULLVP == s_entries[vnodeIndex].vnode || vnodeVid != s_entries[vnodeIndex].vid)
        {
            s_entries[vnodeIndex].vnode = vnode;
            s_entries[vnodeIndex].vid = vnodeVid;
            s_entries[vnodeIndex].virtualizationRoot = VirtualizationRoot_FindForVnode(
                perfTracer,
                cacheMissFallbackFunctionCounter,
                cacheMissFallbackFunctionInnerLoopCounter,
                vnode,
                context);
        }
        
        rootHandle = s_entries[vnodeIndex].virtualizationRoot;
        
        return true;
    }
    
    return false;
}
