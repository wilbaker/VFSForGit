#import <XCTest/XCTest.h>

typedef int16_t VirtualizationRootHandle;

#include "../PrjFSKext/VnodeCacheTestable.hpp"
#include "../PrjFSKext/VnodeCachePrivate.hpp"

@interface VnodeCacheTests : XCTestCase
@end

@implementation VnodeCacheTests

// Dummy vfs_context implementation for vfs_context_t
struct vfs_context
{
};

// Dummy vnode implementation for vnode_t
struct vnode
{
    int dummyData;
};

// Dummy PerfTracer implementation for PerfTracer*
class PerfTracer
{
};

static vnode TestVnode;
static const VirtualizationRootHandle TestRootHandle = 1;

- (void)testComputeVnodeHashKeyWithCapacityOfOne {
    s_entriesCapacity = 1;
    vnode testVnode2;
    vnode testVnode3;
    
    XCTAssertTrue(0 == ComputeVnodeHashKey(&TestVnode));
    XCTAssertTrue(0 == ComputeVnodeHashKey(&testVnode2));
    XCTAssertTrue(0 == ComputeVnodeHashKey(&testVnode3));
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
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == startingIndex);
    
    free(s_entries);
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsFalseWhenCacheFull {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertFalse(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    
    free(s_entries);
}

- (void)testTryFindVnodeIndex_SharedLocked_WrapsToBeginningWhenResolvingCollisions {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    uintptr_t emptyIndex = 2;
    s_entries[emptyIndex].vnode = nullptr;
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    free(s_entries);
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    uintptr_t emptyIndex = s_entriesCapacity - 1;
    s_entries[emptyIndex].vnode = nullptr;
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    free(s_entries);
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReturnsFalseWhenFull {
    s_entriesCapacity = 100;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));

    uintptr_t indexFromHash = 5;
    vnode_t testVnode = &TestVnode;
    uint32_t testVnodeVid = 7;

    XCTAssertFalse(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            true, // invalidateEntry
            TestRootHandle));

    free(s_entries);
}

@end
