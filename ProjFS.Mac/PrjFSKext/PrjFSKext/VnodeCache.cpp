#include <string.h>
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
}

bool VnodeCache::TryInitialize()
{
    if (RWLock_IsValid(this->entriesLock))
    {
        return false;
    }
    
    this->entriesLock = RWLock_Alloc();
    if (!RWLock_IsValid(this->entriesLock))
    {
        return false;
    }

    this->capacity = desiredvnodes * 2;
    if (this->capacity <= 0)
    {
        return false;
    }
    
    this->entries = Memory_AllocArray<VnodeCacheEntry>(this->capacity);
    if (nullptr == this->entries)
    {
        this->capacity = 0;
        return false;
    }
    
    memset(this->entries, 0, this->capacity * sizeof(VnodeCacheEntry));
    
    return true;
}

void VnodeCache::Cleanup()
{
    if (RWLock_IsValid(this->entriesLock))
    {
        RWLock_FreeMemory(&this->entriesLock);
    }

    if (nullptr != this->entries)
    {
        Memory_FreeArray<VnodeCacheEntry>(this->entries, this->capacity);
        this->entries = nullptr;
        this->capacity = 0;
    }
}

// TODO(cache): Add _Nonnull where appropriate
VirtualizationRootHandle VnodeCache::FindRootForVnode(PerfTracer* perfTracer, vfs_context_t context, vnode_t vnode)
{
    VirtualizationRootHandle rootHandle = RootHandle_None;
    uintptr_t startingIndex = this->HashVnode(vnode);
    
    bool lockElevatedToExclusive = false;
    uint32_t vnodeVid = vnode_vid(vnode);
    
    RWLock_AcquireShared(this->entriesLock);
    {
        uintptr_t index = this->FindVnodeIndex_Locked(vnode, startingIndex);
        if (vnode == this->entries[index].vnode)
        {
            // TODO(cache): Also check that the root's vrgid matches what's in the cache
            if (vnodeVid != this->entries[index].vid)
            {
                if (!RWLock_AcquireSharedToExclusive(this->entriesLock))
                {
                    RWLock_AcquireExclusive(this->entriesLock);
                }
                
                lockElevatedToExclusive = true;
                this->UpdateIndexEntryToLatest_Locked(context, perfTracer, index, vnode, vnodeVid);
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
            if (!RWLock_AcquireSharedToExclusive(this->entriesLock))
            {
                RWLock_AcquireExclusive(this->entriesLock);
            }
            
            lockElevatedToExclusive = true;
            
            // 1. Find the insertion index
            // 2. Look up the virtualization root (if still required)
            
            uintptr_t insertionIndex = this->FindVnodeIndex_Locked(vnode, index, startingIndex);
            if (NULLVP == this->entries[insertionIndex].vnode)
            {
                this->UpdateIndexEntryToLatest_Locked(context, perfTracer, index, vnode, vnodeVid);
                rootHandle = this->entries[index].virtualizationRoot;
            }
            else if (insertionIndex == startingIndex)
            {
                // TODO(cache): Entry is not in the cache and there is no room
            }
            else
            {
                // We found an existing entry, ensure it's still valid
                // TODO(cache): Also check that the root's vrgid matches what's in the cache
                if (vnodeVid != this->entries[index].vid)
                {
                    this->UpdateIndexEntryToLatest_Locked(context, perfTracer, index, vnode, vnodeVid);
                }
                
                rootHandle = this->entries[index].virtualizationRoot;
            }
        }
    }
    
    if (lockElevatedToExclusive)
    {
        RWLock_ReleaseExclusive(this->entriesLock);
    }
    else
    {
        RWLock_ReleaseShared(this->entriesLock);
    }
    
    return rootHandle;
}

uintptr_t VnodeCache::HashVnode(vnode_t vnode)
{
    uintptr_t vnodeAddress = reinterpret_cast<uintptr_t>(vnode);
    return (vnodeAddress >> 3) % this->capacity;
}

uintptr_t VnodeCache::FindVnodeIndex_Locked(vnode_t vnode, uintptr_t startingIndex)
{
    return this->FindVnodeIndex_Locked(vnode, startingIndex, startingIndex);
}

uintptr_t VnodeCache::FindVnodeIndex_Locked(vnode_t vnode, uintptr_t startingIndex, uintptr_t stoppingIndex)
{
    // Walk from the starting index until we find:
    //    -> The vnode
    //    -> A NULLVP entry
    //    -> The stopping index
    // If we hit the end of the array, continue searching from the start
    uintptr_t index = startingIndex;
    while (vnode != this->entries[index].vnode)
    {
        if (NULLVP == this->entries[index].vnode)
        {
            break;
        }
    
        index = (index + 1) % this->capacity;
        if (index == stoppingIndex)
        {
            // Looped through the entire cache and didn't find an empty slot or the vnode
            break;
        }
    }
    
    return index;
}

void VnodeCache::UpdateIndexEntryToLatest_Locked(
    vfs_context_t context,
    PerfTracer* perfTracer,
    uintptr_t index,
    vnode_t vnode,
    uint32_t vnodeVid)
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
}
