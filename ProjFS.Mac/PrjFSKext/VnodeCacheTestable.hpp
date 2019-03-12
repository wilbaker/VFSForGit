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

KEXT_STATIC bool TryFindVnodeIndex_Locked(
    vnode_t _Nonnull vnode,
    uintptr_t vnodeHash,
    /* out parameters */
    uintptr_t& vnodeIndex);

KEXT_STATIC bool TryUpdateEntryToLatest_ExclusiveLocked(
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

// Static variables used for maintaining Vnode cache state
extern uint32_t s_entriesCapacity;
extern VnodeCacheEntry* _Nullable s_entries;
