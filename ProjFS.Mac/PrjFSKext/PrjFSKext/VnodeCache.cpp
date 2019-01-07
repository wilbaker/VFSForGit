#include "kernel-header-wrappers/vnode.h"
#include "VnodeUtilities.hpp"
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

// TODO(cache): Add _Nonnull where appropriate
VirtualizationRootHandle VnodeCache::FindRootForVnode(PerfTracer* perfTracer, vfs_context_t context, vnode_t vnode)
{
    VirtualizationRootHandle rootHandle = RootHandle_None;
    
    uintptr_t vnodeAddress = reinterpret_cast<uintptr_t>(vnode);
    uintptr_t startingIndex = vnodeAddress >> 3 % this->capacity;
    
    bool cacheUpdated = false;
    
    RWLock_AcquireShared(this->entriesLock);
    {
        // Walk from the starting index until we find:
        // -> The vnode
        // -> A NULLVP entry
        // -> The end of the array (at which point we need to start searching from the top)
        uintptr_t index = startingIndex;
        while (vnode != this->entries[index].vnode)
        {
            if (NULLVP == this->entries[index].vnode)
            {
                break;
            }
        
            index = (index + 1) % this->capacity;
            if (index == startingIndex)
            {
                // Looped through the entire cache and didn't find an empty slot or the vnode
                break;
            }
        }
        
        if (vnode == this->entries[index].vnode)
        {
            // We found the vnode we're looking for, check if it's still valid and update it if required
            uint32_t vnodeVid = vnode_vid(vnode);
            if (vnodeVid != this->entries[index].vid)
            {
                // TODO(cache): Also check that the root's vrgid matches what's in the cache
                cacheUpdated = true;
                if (!RWLock_AcquireSharedToExclusive(this->entriesLock))
                {
                    RWLock_AcquireExclusive(this->entriesLock);
                }
                
                FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(vnode, context);
                
                // TODO(cache): Add proper perf points
                this->entries[index].virtualizationRoot = VirtualizationRoot_FindForVnode(
                    perfTracer,
                    PrjFSPerfCounter_VnodeOp_FindRoot,
                    PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
                    vnode,
                    vnodeFsidInode);
                
                this->entries[index].vid = vnodeVid;
            }
            
            rootHandle = this->entries[index].virtualizationRoot;
        }
        else if (index == startingIndex)
        {
            // TODO(cache): Entry is not in the cache and there is no room
        }
        else
        {
            // We need to insert the vnode into the cache, upgrade to exclusive lock and add it to the cache
            
            cacheUpdated = true;
            if (!RWLock_AcquireSharedToExclusive(this->entriesLock))
            {
                RWLock_AcquireExclusive(this->entriesLock);
            }
            
            // 1. Find the insertion index
            // 2. Look up the virtualization root (if still required)
            
            uintptr_t insertionIndex = index;
            while (vnode != this->entries[insertionIndex].vnode)
            {
                if (NULLVP == this->entries[insertionIndex].vnode)
                {
                    break;
                }
            
                insertionIndex = (insertionIndex + 1) % this->capacity;
                if (insertionIndex == startingIndex)
                {
                    // Looped through the entire cache and didn't find an empty slot or the vnode
                    break;
                }
            }
            
            uint32_t vnodeVid = vnode_vid(vnode);

            if (NULLVP == this->entries[insertionIndex].vnode)
            {
                FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(vnode, context);
                
                // TODO(cache): Add proper perf points
                this->entries[index].virtualizationRoot = VirtualizationRoot_FindForVnode(
                    perfTracer,
                    PrjFSPerfCounter_VnodeOp_FindRoot,
                    PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
                    vnode,
                    vnodeFsidInode);
            
                this->entries[index].vid = vnodeVid;
                
                // TODO(cache): Also set the vrgid
                
                rootHandle = this->entries[index].virtualizationRoot;
            }
            else if (insertionIndex == startingIndex)
            {
                // TODO(cache): Entry is not in the cache and there is no room
            }
            else
            {
                // We found an existing entry, ensure it's still valid
                                if (vnodeVid != this->entries[index].vid)
                {
                    // TODO(cache): Also check that the root's vrgid matches what's in the cache
                    FsidInode vnodeFsidInode = Vnode_GetFsidAndInode(vnode, context);
                    
                    // TODO(cache): Add proper perf points
                    this->entries[index].virtualizationRoot = VirtualizationRoot_FindForVnode(
                        perfTracer,
                        PrjFSPerfCounter_VnodeOp_FindRoot,
                        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
                        vnode,
                        vnodeFsidInode);
                    
                    this->entries[index].vid = vnodeVid;
                }
                
                rootHandle = this->entries[index].virtualizationRoot;
            }
        }
    }
    
    if (cacheUpdated)
    {
        RWLock_ReleaseExclusive(this->entriesLock);
    }
    else
    {
        RWLock_ReleaseShared(this->entriesLock);
    }
    
    return rootHandle;
}
