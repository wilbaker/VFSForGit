#include "public/PrjFSCommon.h"
#include "public/FsidInode.h"
#include "public/PrjFSPerfCounter.h"
#include <sys/kernel_types.h>

#ifndef __cplusplus
#error None of the kext code is set up for being called from C or Objective-C; change the including file to C++ or Objective-C++
#endif

#ifndef KEXT_UNIT_TESTING
#error This class should only be called for unit tests
#endif

// Forward declarations for unit testing
class PerfTracer;
struct VnodeCacheEntry;

KEXT_STATIC_INLINE void InvalidateCache_ExclusiveLocked();
KEXT_STATIC_INLINE uintptr_t HashVnode(vnode_t _Nonnull vnode);

KEXT_STATIC bool TryFindVnodeIndex_SharedLocked(
    vnode_t _Nonnull vnode,
    uintptr_t startingIndex,
    uintptr_t stoppingIndex,
    /* out parameters */
    uintptr_t& cacheIndex);

KEXT_STATIC void UpdateCacheEntryToLatest_ExclusiveLocked(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter cacheMissFallbackFunctionCounter,
    PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
    uintptr_t index,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode,
    uint32_t vnodeVid);

KEXT_STATIC bool FindAndUpdateEntryToLatest_ExclusiveLocked(
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

// Static variables used for maintaining Vnode cache state
extern uint32_t s_entriesCapacity;
extern VnodeCacheEntry* _Nullable s_entries;

