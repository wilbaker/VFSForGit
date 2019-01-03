#pragma once

#include <sys/kernel_types.h>

class VnodeCache
{
public:
    VnodeCache();
    ~VnodeCache();
    
    VirtualizationRootHandle FindRootForVnode(vnode_t vnode);
    
private:
    VnodeCache(const VnodeCache&) = delete;
    VnodeCache& operator=(const VnodeCache&) = delete;
    
    struct VnodeCacheEntry
    {
        vnode_t vnode;
        uint32_t vid;   // vnode generation number
        uint16_t vrgid; // virtualization root generation number
        VirtualizationRootHandle virtualizationRoot;
    };
    
    VnodeCacheEntry* entries;
};
