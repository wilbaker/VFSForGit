#include "public/PrjFSCommon.h"
#include <sys/kernel_types.h>

#ifndef __cplusplus
#error None of the kext code is set up for being called from C or Objective-C; change the including file to C++ or Objective-C++
#endif

#ifndef KEXT_UNIT_TESTING
#error This class should only be called for unit tests
#endif

KEXT_STATIC_INLINE void InvalidateCache_ExclusiveLocked();
KEXT_STATIC_INLINE uintptr_t HashVnode(vnode_t _Nonnull vnode);

KEXT_STATIC bool TryFindVnodeIndex_SharedLocked(
    vnode_t _Nonnull vnode,
    uintptr_t startingIndex,
    uintptr_t stoppingIndex,
    /* out parameters */
    uintptr_t& cacheIndex);

struct VnodeCacheEntry;
extern uint32_t s_entriesCapacity;
extern VnodeCacheEntry* _Nullable s_entries;

