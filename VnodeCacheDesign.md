# Vnode Cache Design

## Goals

- Reduce the time required to determine if a file is inside a virtualization root
- Reduce the time required to find a file's virtualization root
- Eliminate the use of file flags to determine if a file is inside of a root

## Design 

### Cached Data

PrjFSKext will cache the following information for each `vnode_t` pointer:

- The `vnode_t` itself
- The vnode's `uint32_t` vid (vnode generation number)
- A `uint32_t` virtualization\cache generation number (hereby referred to as `vrgid`).  If a cache entry's `vrgid` does not match its roots it means that all entries for that root have been invalidated and the `VirtualizationRootHandle` in the cache entry is no longer valud.  There will also be a global `vrgid`  for all vnodes that are not inside of any virtualization roots.
- `VirtualizationRootHandle` for the vnode (or `RootHandle_None` if the vnode is not in a root).  **NOTE** This assumes that `VirtualizationRootHandle`  are not recycled as VFS4G instances are unmounted\remounted.

The value of the `vnode_t` pointer will serve a the key to the cache.

### Hash Table

The data for each vnode will be stored in a simplified hash table with the `vnode_t` pointer serving as the key.

#### Collision Handling

Index collisions will be handled using open addressing.  The exact technique (e.g. linear probing, double hashing, etc.) will be determined once we have sample sets of `vnode_t` pointers and can evaluate the quality of our hash function.

#### Size

The hash table will be a fixed size of length `desiredvnodes`.

#### Hash Function

The hashing function to use for the vnode cache is still an open question, and will be determined after collecting same `vnode_t` pointer data (to test different hashing functions against).

Per @pmj, `vnode_t` pointers are not arbitrary, as vnodes use the zone allocator:

>If I'm reading the output of zprint correctly, vnodes are 248 bytes on macOS 10.13.6, and allocated in 8KiB slabs. (33 vnodes per slab) So each vnode pointer will have one of only 33 values in the low 13 bits (uniformly distributed), a whole bunch more non-uniform entropy in bits 13 and up, at least to a point; there will probably be very little entropy in the upper ~32 bits. [...] this sort of pattern is poison for the identity hash function if using power-of-2 capacities, but it really shouldn't be hard to come up with a hash function that works well. 

#### Other Data Structures to Consider

A radix/prefix tree is another option for caching the data.  Whether we go with this option or a hashing based approach will be based on the results of performance testing.

### Using the Cache

When PrjFSKext needs to determine the virtualization root of a file (or if a file is inside of a virtualization root at all), it will look up the vnode in its cache.  

At a high level, there are two possible outcomes when looking up a vnode in the cache:

- The vnode exists in the cache
- The vnode does not exist in the cache

However, because vnodes are recycled, the cache will need to consider three possible states for vnodes:

- The vnode exists in the cache, its `vid` has not changed, and its `vrgid` matches the `vrgid` of its virtualization root:  PrjFSKext will use the `VirtualizationRootHandle` in the cache.

- The vnode exists in the cache and either its `vid` has changed or its `vrgid` no longer matches the `vrgid` of its virtualization root:  The vnode has been recycled or the `vrgid` of its virtualization root has changed. PrjFSKext needs to look up the vnode's current `VirtualizationRootHandle` and update the cache entry.

- The vnode does not exist in the cache:  PrjFSKext will lookup its `VirtualizationRootHandle` and insert the vnode into the cache.

When PrjFSKext hits one of the cache miss scenarios (2nd and 3rd bullet points above), it can find the vnode's virtualization root by walking up the vnode tree until:
PrjFSKext can find the new root by walking up the tree until:
- There's a cache hit
- It encounters a virtualization root's vnode
- It encounters the root vnode

### Special Case: Renames

*Directory renames* 

Directory renames are a special case because all of the directory's children may need to have their `VirtualizationRootHandle` updated. Rather than recording\tracking all of the parent\child relationships among the vnodes PrjFSKext will (potentially) invalidate a portion of the vnod cache:

- The rename is within the same virtualization root: All entries in the cache are still valid.

- The rename source and target are outside of any virutalization roots: All entries in the cache are still valid.

- The rename moves a directory out of a virtualization root: Increase that root's `vrgid` (thereby invalidating all entries in the cache for that root)

- The rename moves a directory from outside of any virtualization root to inside a virtualization root: Increase the global "no root" `vrgid` (thereby invalidating all entries in the cache that are not inside of a virtualizaton root)

If PrjFSKext is unable to determine if the rename was within the same root (e.g. the directory's vnode was recycled) then PrjFSKext will invalidate the cache.

*File renames*

When a file is renamed PrjFSKext will update its `VirtualizationRootHandle` and\or `vid` as needed.

## Future Enhancements

- The vnode cache could track which vnodes have received the `KAUTH_FILEOP_WILL_RENAME` file operation (on Mojave) so that when handling `KAUTH_VNODE_DELETE` the kext can know if the delete vnode operation is for a rename.
- The vnode cache could record whether or not a file is empty.  This could allow for eliminating the use of the `FileFlags_IsEmpty` flag (the xattr would continue to be the source of truth as to whether the file has been hydrated or not).

## What is not Addressed by the Cache

- Improving the speed and reliability of finding a vnode's path.  The vnode cache does not store anything path related, and this would be better addressed by other options (e.g. use fsid+inode instead of path, or store the path in a file's xattrs) 
- Addressing failures to get a vnode's parent (when the vnode is not in the cache)
- Finding the roots for hard links that span repos (and general handling of hard links that cross repo boundaries)

## Open Questions

- Will PrjFSKext invalidate the cache too frequently if it does so on every directory rename?
- How much of a perf hit do we see after invalidating a portion of the cache?
