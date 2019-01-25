#import <XCTest/XCTest.h>

typedef int16_t VirtualizationRootHandle;

#include "../PrjFSKext/VnodeCacheTestable.hpp"

@interface VnodeCacheTests : XCTestCase
@end

@implementation VnodeCacheTests

typedef int16_t VirtualizationRootHandle;
struct VnodeCacheEntry
{
    vnode_t vnode;
    uint32_t vid;   // vnode generation number
    VirtualizationRootHandle virtualizationRoot;
};

static const VirtualizationRootHandle TestVirtualizationRootHandle = 3;

VirtualizationRootHandle VirtualizationRoot_FindForVnode(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter functionCounter,
    PrjFSPerfCounter innerLoopCounter,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode);

VirtualizationRootHandle VirtualizationRoot_FindForVnode(
    PerfTracer* _Nonnull perfTracer,
    PrjFSPerfCounter functionCounter,
    PrjFSPerfCounter innerLoopCounter,
    vnode_t _Nonnull vnode,
    const FsidInode& vnodeFsidInode)
{
    return TestVirtualizationRootHandle;
}

- (void)testHashVnodeWithCapacityOfOne {
    s_entriesCapacity = 1;
    XCTAssertTrue(0 == HashVnode(reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1))));
}

- (void)testInvalidateCache_ExclusiveLocked_SetsMemoryToZeros {
    s_entriesCapacity = 100;
    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    InvalidateCache_ExclusiveLocked();
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    
    free(s_entries);
    free(emptyArray);
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsStartingIndexWhenNull {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    
    vnode_t testVnode = reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1));
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_SharedLocked(testVnode, startingIndex, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == startingIndex);
    
    free(s_entries);
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsFalseWhenCacheFull {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    
    vnode_t testVnode = reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1));
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertFalse(TryFindVnodeIndex_SharedLocked(testVnode, startingIndex, startingIndex, /* out */ cacheIndex));
    
    free(s_entries);
}

- (void)testTryFindVnodeIndex_SharedLocked_WrapsToBeginningWhenResolvingCollisions {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    uintptr_t emptyIndex = 2;
    s_entries[emptyIndex].vnode = nullptr;
    
    vnode_t testVnode = reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1));
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_SharedLocked(testVnode, startingIndex, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    free(s_entries);
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    uintptr_t emptyIndex = s_entriesCapacity - 1;
    s_entries[emptyIndex].vnode = nullptr;
    
    vnode_t testVnode = reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1));
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_SharedLocked(testVnode, startingIndex, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    free(s_entries);
}

- (void)testUpdateCacheEntryToLatest_ExclusiveLocked_UpdatesCache {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    
    PerfTracer* dummyPerfTracerPointer = reinterpret_cast<PerfTracer*>(static_cast<uintptr_t>(1));
    uintptr_t cacheIndex = 5;
    vnode_t testVnode = reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1));
    uint32_t testVnodeVid = 7;
    FsidInode testVnodeFsidInode;
    
    XCTAssertTrue(s_entries[cacheIndex].vnode == nullptr);
    XCTAssertTrue(s_entries[cacheIndex].vid == 0);
    
    UpdateCacheEntryToLatest_ExclusiveLocked(
        dummyPerfTracerPointer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        cacheIndex,
        testVnode,
        testVnodeFsidInode,
        testVnodeVid);
    
    XCTAssertTrue(s_entries[cacheIndex].vnode == testVnode);
    XCTAssertTrue(s_entries[cacheIndex].vid == testVnodeVid);
    XCTAssertTrue(s_entries[cacheIndex].virtualizationRoot == TestVirtualizationRootHandle);
    
    free(s_entries);
}

- (void)testFindAndUpdateEntryToLatest_ExclusiveLocked_ReturnsFalseWhenFull {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    
    PerfTracer* dummyPerfTracerPointer = reinterpret_cast<PerfTracer*>(static_cast<uintptr_t>(1));
    uintptr_t indexFromHash = 5;
    vnode_t testVnode = reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1));
    uint32_t testVnodeVid = 7;
    FsidInode testVnodeFsidInode;
    VirtualizationRootHandle rootHandle;
    
    XCTAssertFalse(
        FindAndUpdateEntryToLatest_ExclusiveLocked(
            dummyPerfTracerPointer,
            PrjFSPerfCounter_VnodeOp_FindRoot,
            PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
            testVnode,
            testVnodeFsidInode,
            indexFromHash,
            indexFromHash,
            testVnodeVid,
            true, // invalidateEntry
            /* out paramaeters */
            rootHandle));
    
    free(s_entries);
}

@end
