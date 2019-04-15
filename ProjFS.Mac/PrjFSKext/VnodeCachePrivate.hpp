#pragma once

#include "kernel-header-wrappers/stdatomic.h"

enum UpdateCacheBehavior
{
    UpdateCacheBehavior_Invalid = 0,
    
    // If the current entry is up-to-date it will be used
    UpdateCacheBehavior_TrustCurrentEntry,
    
    // The current entry will be replaced with the new root
    UpdateCacheBehavior_ForceRefresh,
    
    // The current entry will have its root marked as invalid (forcing the next lookup to find the root)
    UpdateCacheBehavior_InvalidateEntry,
};

struct VnodeCacheEntry
{
    vnode_t vnode;
    uint32_t vid;   // vnode generation number
    VirtualizationRootHandle virtualizationRoot;
};

struct VnodeCacheHealthStats
{
    _Atomic(uint32_t) cacheEntries;
    _Atomic(uint64_t) invalidateEntireCacheCount;
    _Atomic(uint64_t) totalCacheLookups;
    _Atomic(uint64_t) totalLookupCollisions;
    _Atomic(uint64_t) totalFindRootForVnodeHits;
    _Atomic(uint64_t) totalFindRootForVnodeMisses;
    _Atomic(uint64_t) totalRefreshRootForVnode;
    _Atomic(uint64_t) totalInvalidateVnodeRoot;
};

// Allow cache the cache to use between 4 MB and 64 MB of memory (assuming 16 bytes per VnodeCacheEntry)
KEXT_STATIC const uint32_t MinPow2VnodeCacheCapacity = 0x040000;
KEXT_STATIC const uint32_t MaxPow2VnodeCacheCapacity = 0x400000;
