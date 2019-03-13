#import <XCTest/XCTest.h>

typedef int16_t VirtualizationRootHandle;

#include "../PrjFSKext/VnodeCache.hpp"
#include "../PrjFSKext/VnodeCacheTestable.hpp"
#include "../PrjFSKext/VnodeCachePrivate.hpp"

#define XCTAssertTrueHelper(test, expression, ...) \
    _XCTPrimitiveAssertTrue(test, expression, @#expression, __VA_ARGS__)

@interface AssertHelper : NSObject
{
}

- (void) AssertTrue:(XCTestCase*)test theExpression:(bool) expression;
@end

@implementation AssertHelper
- (void) AssertTrue:(XCTestCase*)test theExpression:(bool) expression{
    XCTAssertTrueHelper(test, expression);
}
@end

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



class Should
{
public:
    Should(XCTestCase* test)
        : testCase(test)
    {
        assertHelper = [[AssertHelper alloc] init];
    }
    
    ~Should()
    {
    }
    
    void BeTrue(bool expression)
    {
        [(id)assertHelper AssertTrue:testCase theExpression:expression];
    }
    
private:
    XCTestCase* testCase;
    AssertHelper* assertHelper;
};

static vnode TestVnode;
static const VirtualizationRootHandle TestRootHandle = 1;
static Should* should;

static void AllocateCacheEntries(uint32_t capacity, bool fillCache)
{
    s_entriesCapacity = capacity;
    s_entries = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    
    if (fillCache)
    {
        // memsetting a value of 1 across the entire array ensure there's will be no VnodeCacheEntrys with
        // a null vnode entry
        memset(s_entries, 1, s_entriesCapacity * sizeof(VnodeCacheEntry));
    }
}

static void FreeCacheEntries()
{
    s_entriesCapacity = 0;
    free(s_entries);
}

static void ShouldBeTrue(bool expression)
{
    should->BeTrue(expression);
}

static void MarkEntryAsFree(VnodeCacheTests* testCase, uintptr_t entryIndex)
{
    ShouldBeTrue(entryIndex < s_entriesCapacity);
    s_entries[entryIndex].vnode = nullptr;
}

- (void)setUp
{
    should = new Should(self);
}

- (void)tearDown
{
    delete should;
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
    
    VnodeCache_InvalidateCache(nullptr);
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

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsStartingIndexWhenNull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == startingIndex);
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsFalseWhenCacheFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertFalse(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_SharedLocked_WrapsToBeginningWhenResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t emptyIndex = 2;
    MarkEntryAsFree(self, emptyIndex);
    
    vnode_t testVnode = &TestVnode;
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode, startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_SharedLocked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    uintptr_t emptyIndex = s_entriesCapacity - 1;
    MarkEntryAsFree(self, emptyIndex);
    
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
            true, // invalidateEntry
            TestRootHandle));

    FreeCacheEntries();
}

@end
