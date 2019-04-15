#pragma once

struct PrjFSHealthData
{
    uint32_t cacheCapacity;
    uint32_t cacheEntries;
    uint64_t invalidateEntireCacheCount;
    uint64_t totalSearches;
    uint64_t totalProbingSteps;
    uint64_t totalFindRootForVnodeHits;
    uint64_t totalFindRootForVnodeMisses;
    uint64_t totalRefreshRootForVnode;
    uint64_t totalInvalidateVnodeRoot;
};
