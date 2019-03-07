#pragma once

#include <sys/kernel_types.h>
#include "VirtualizationRoots.hpp"

kern_return_t VnodeCache_Init();

void VnodeCache_Cleanup();

VirtualizationRootHandle VnodeCache_FindRootForVnode(
        PerfTracer* _Nonnull perfTracer,
        PrjFSPerfCounter cacheHitCounter,
        PrjFSPerfCounter cacheMissCounter,
        PrjFSPerfCounter cacheMissFallbackFunctionCounter,
        PrjFSPerfCounter cacheMissFallbackFunctionInnerLoopCounter,
        vnode_t _Nonnull vnode,
        vfs_context_t _Nonnull context,
        bool invalidateEntry);

void VnodeCache_InvalidateCache(PerfTracer* _Nullable perfTracer);