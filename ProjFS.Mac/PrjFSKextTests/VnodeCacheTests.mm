#import <XCTest/XCTest.h>

typedef int16_t VirtualizationRootHandle;

#include "KextLogMock.h"
#include "KextMockUtilities.hpp"
#include "../PrjFSKext/VnodeCache.hpp"
#include "../PrjFSKext/VnodeCachePrivate.hpp"
#include "../PrjFSKext/VnodeCacheTestable.hpp"

using KextMock::_;

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

static vnode TestVnode;
static const VirtualizationRootHandle TestRootHandle = 1;
static const VirtualizationRootHandle TestSecondRootHandle = 2;

static void AllocateCacheEntries(uint32_t capacity, bool fillCache);
static void FreeCacheEntries();
static void MarkEntryAsFree(uintptr_t entryIndex);

- (void) setUp
{
}

- (void) tearDown
{
    MockCalls::Clear();
}

- (void)testComputePow2CacheCapacity {

    // At a minimum ComputePow2CacheCapacity should return the minimum value in AllowedPow2CacheCapacities
    XCTAssertTrue(MinPow2VnodeCacheCapacity == ComputePow2CacheCapacity(0));
    
    // ComputePow2CacheCapacity should round up to the nearest power of 2 (after multiplying expectedVnodeCount by 2)
    int expectedVnodeCount = MinPow2VnodeCacheCapacity/2 + 1;
    XCTAssertTrue(MinPow2VnodeCacheCapacity << 1 == ComputePow2CacheCapacity(expectedVnodeCount));
    
    // ComputePow2CacheCapacity should be capped at the maximum value in AllowedPow2CacheCapacities
    XCTAssertTrue(MaxPow2VnodeCacheCapacity == ComputePow2CacheCapacity(MaxPow2VnodeCacheCapacity + 1));
}

- (void)testComputeVnodeHashKeyWithCapacityOfOne {
    s_entriesCapacity = 1;
    vnode testVnode2;
    vnode testVnode3;
    
    XCTAssertTrue(0 == ComputeVnodeHashIndex(&TestVnode));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(&testVnode2));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(&testVnode3));
}

- (void)testVnodeCache_InvalidateCache_SetsMemoryToZeros {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    PerfTracer dummyPerfTracer;
    VnodeCache_InvalidateCache(&dummyPerfTracer);
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    
    free(emptyArray);
    FreeCacheEntries();
}

- (void)testInvalidateCache_ExclusiveLocked_SetsMemoryToZeros {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    InvalidateCache_ExclusiveLocked();
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    
    free(emptyArray);
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_ReturnsStartingIndexWhenNull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == startingIndex);
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_ReturnsFalseWhenCacheFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertFalse(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_WrapsToBeginningWhenResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t emptyIndex = 2;
    MarkEntryAsFree(emptyIndex);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    uintptr_t emptyIndex = s_entriesCapacity - 1;
    MarkEntryAsFree(emptyIndex);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReturnsFalseWhenFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);

    uintptr_t indexFromHash = 5;
    vnode_t testVnode = &TestVnode;
    uint32_t testVnodeVid = 7;

    XCTAssertFalse(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            TestRootHandle));

    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesIndeterminateEntry {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    vnode_t testVnode = &TestVnode;
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testVnode);
    uint32_t testVnodeVid = 7;

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            RootHandle_Indeterminate));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);

    PerfTracer dummyPerfTracer;
    vfs_context dummyContext;
    
    VnodeCache_FindRootForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testVnode,
        &dummyContext);

    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_LogsErrorWhenCacheHasDifferentRoot {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    vnode_t testVnode = &TestVnode;
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testVnode);
    uint32_t testVnodeVid = 7;

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode,
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);

    XCTAssertTrue(MockCalls::DidCallFunction(KextMessageLogged, KEXTLOG_ERROR));

    FreeCacheEntries();
}

// TryInsertOrUpdateEntry_ExclusiveLocked
// VnodeCache_FindRootForVnode
// VnodeCache_RefreshRootForVnode
// VnodeCache_InvalidateVnodeAndGetLatestRoot
// TryGetVnodeRootFromCache
// LookupVnodeRootAndUpdateCache

static void AllocateCacheEntries(uint32_t capacity, bool fillCache)
{
    s_entriesCapacity = capacity;
    s_entries = new VnodeCacheEntry[s_entriesCapacity];
    
    static vnode dummyNode;
    for (uint32_t i = 0; i < s_entriesCapacity; ++i)
    {
        if (fillCache)
        {
            s_entries[i].vnode = &dummyNode;
        }
        else
        {
            memset(&(s_entries[i]), 0, sizeof(VnodeCacheEntry));
        }
    }
}

static void FreeCacheEntries()
{
    s_entriesCapacity = 0;
    delete[] s_entries;
}

static void MarkEntryAsFree(uintptr_t entryIndex)
{
    s_entries[entryIndex].vnode = nullptr;
}

@end