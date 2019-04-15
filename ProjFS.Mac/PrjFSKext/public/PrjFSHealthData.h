#pragma once

struct PrjFSHealthData
{
    uint32_t cacheCapacity;
    uint32_t cacheEntries;
    uint64_t invalidateEntireCacheCount;
    uint64_t totalProbingSteps;
    uint64_t totalSearches;
    uint64_t totalLookupHits;
    uint64_t totalLookupMisses;
    uint64_t totalVnodeRefreshes;
    uint64_t totalVnodeInvalidations;
};
