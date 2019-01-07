#include "kernel-header-wrappers/vnode.h"
#include "VnodeCache.hpp"
#include "Memory.hpp"

VnodeCache::VnodeCache()
    : capacity(0)
    , entries(nullptr)
{
}

VnodeCache::~VnodeCache()
{
    if (RWLock_IsValid(this->entriesLock))
    {
        RWLock_FreeMemory(&this->entriesLock);
    }

    if (nullptr != this->entries)
    {
        Memory_Free(this->entries, sizeof(VnodeCacheEntry) * this->capacity);
        this->capacity = 0;
    }
}

bool VnodeCache::TryInitialize()
{
    if (RWLock_IsValid(this->entriesLock))
    {
        return KERN_FAILURE;
    }
    
    this->entriesLock = RWLock_Alloc();
    if (!RWLock_IsValid(this->entriesLock))
    {
        return KERN_FAILURE;
    }

    this->capacity = desiredvnodes * 2;
    this->entries = static_cast<VnodeCacheEntry*>(Memory_Alloc(sizeof(VnodeCacheEntry) * this->capacity));
    if (nullptr == this->entries)
    {
        this->capacity = 0;
        return false;
    }
    
    return true;
}

VirtualizationRootHandle VnodeCache::FindRootForVnode(vnode_t vnode)
{
    uintptr_t vnodeAddress = reinterpret_cast<uintptr_t>(vnode);
    uintptr_t startingIndex = vnodeAddress >> 3 % this->capacity;
    
    RWLock_AcquireShared(this->entriesLock);
    {
        if (vnode == this->entries[startingIndex].vnode)
        {
            return 0;
        }
    }
    RWLock_ReleaseShared(this->entriesLock);
    
    return 0;
}
