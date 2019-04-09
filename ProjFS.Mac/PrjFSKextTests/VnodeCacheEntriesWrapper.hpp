#pragma once

#include "../PrjFSKext/VirtualizationRoots.hpp"
#include "../PrjFSKext/VnodeCachePrivate.hpp"
#include "../PrjFSKext/VnodeCacheTestable.hpp"

// Helper class for interacting with s_entries and s_entriesCapacity
class VnodeCacheEntriesWrapper
{
public:
    VnodeCacheEntriesWrapper(const bool fillCache)
    {
        s_entriesCapacity = 64;
        s_ModBitmask = s_entriesCapacity - 1;
        s_entries = new VnodeCacheEntry[s_entriesCapacity];
        
        this->dummyMount = mount::Create();
        this->dummyNode = dummyMount->CreateVnodeTree("/DUMMY");
        if (fillCache)
        {
            this->FillAllEntries();
        }
        else
        {
            for (uint32_t i = 0; i < s_entriesCapacity; ++i)
            {
                memset(&(s_entries[i]), 0, sizeof(VnodeCacheEntry));
            }
        }
    }
    
    ~VnodeCacheEntriesWrapper()
    {
        s_entriesCapacity = 0;
        delete[] s_entries;
    }
    
    void FillAllEntries()
    {
        for (uint32_t i = 0; i < s_entriesCapacity; ++i)
        {
            s_entries[i].vnode = this->dummyNode.get();
        }
    }
    
    void MarkEntryAsFree(const uintptr_t entryIndex)
    {
        s_entries[entryIndex].vnode = nullptr;
    }
    
    VnodeCacheEntry& operator[] (const uintptr_t entryIndex)
    {
        return s_entries[entryIndex];
    }
    
    uint32_t GetCapacity() const
    {
        return s_entriesCapacity;
    }
    
private:
    std::shared_ptr<mount> dummyMount;
    std::shared_ptr<vnode> dummyNode;
};
