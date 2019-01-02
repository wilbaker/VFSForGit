#pragma once

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
    };
    
    VnodeCacheEntry* entries;
};
