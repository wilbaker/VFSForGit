#import <XCTest/XCTest.h>
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

- (void)testHashVnodeWithCapacityOfOne {
    s_entriesCapacity = 1;
    XCTAssertTrue(0 == HashVnode(reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1))));
}

- (void)testInvalidateCache_ExclusiveLockedSetsMemoryToZeros {
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

@end
