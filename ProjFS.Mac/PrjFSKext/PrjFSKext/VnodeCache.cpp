#include "vnode.h"
#include "VnodeCache.hpp"
#include "Memory.hpp"

VnodeCache::VnodeCache()
    : capacity(0)
    , entries(nullptr)
{
}

VnodeCache::~VnodeCache()
{
    if (nullptr != this->entries)
    {
        Memory_Free(this->entries, sizeof(VnodeCacheEntry) * this->capacity);
        this->capacity = 0;
    }
}

bool VnodeCache::TryInitialize()
{
    this->capacity = desiredvnodes * 2;
    this->entries = static_cast<VnodeCacheEntry*>(Memory_Alloc(sizeof(VnodeCacheEntry) * this->capacity));
    if (nullptr == this->entries)
    {
        this->capacity = 0;
        return false;
    }
    
    return true;
}
