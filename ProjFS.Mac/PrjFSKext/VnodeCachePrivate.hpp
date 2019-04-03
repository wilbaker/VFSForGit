#pragma once

struct VnodeCacheEntry
{
    vnode_t vnode;
    uint32_t vid;   // vnode generation number
    VirtualizationRootHandle virtualizationRoot;
};

// Allow cache the cache to use between 4 MB and 64 MB of memory (assuming 16 bytes per VnodeCacheEntry)
KEXT_STATIC const uint32_t MinPow2VnodeCacheCapacity = 0x040000;
KEXT_STATIC const uint32_t MaxPow2VnodeCacheCapacity = 0x400000;
