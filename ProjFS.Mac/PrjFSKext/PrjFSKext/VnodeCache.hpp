#pragma once

#include <sys/kernel_types.h>

#include "VirtualizationRoots.hpp"
#include "Locks.hpp"

class VnodeCache
{
public:
    VnodeCache();
    ~VnodeCache();
    
    bool TryInitialize();
    
    // TODO(cache): Also pass back fsid and inode
    VirtualizationRootHandle FindRootForVnode(
        PerfTracer* perfTracer,
        vfs_context_t context,
        vnode_t vnode);
    
private:
    VnodeCache(const VnodeCache&) = delete;
    VnodeCache& operator=(const VnodeCache&) = delete;
    
    uintptr_t HashVnode(vnode_t vnode);
    uintptr_t FindVnodeIndex_Locked(vnode_t vnode, uintptr_t startingIndex);
    
    struct VnodeCacheEntry
    {
        vnode_t vnode;
        uint32_t vid;   // vnode generation number
        //uint16_t vrgid; // TODO(cache): virtualization root generation number
        VirtualizationRootHandle virtualizationRoot;
    };
    
    // Number of VnodeCacheEntry that can be stored in entries
    uint32_t capacity;
    
    VnodeCacheEntry* entries;
    RWLock entriesLock;
};
