#pragma once

#include <sys/kernel_types.h>

class VnodeCache
{
public:
    VnodeCache();
    ~VnodeCache();
    
private:
    VnodeCache(const VnodeCache&) = delete;
    VnodeCache& operator=(const VnodeCache&) = delete;
    
    struct VnodeCacheEntry
    {
        vnode_t vnode;
    };
    
    VnodeCacheEntry* entries;
};
