#import <XCTest/XCTest.h>
#include "../PrjFSKext/VnodeCacheTestable.hpp"

@interface VnodeCacheTests : XCTestCase
@end

@implementation VnodeCacheTests

- (void)testHashVnodeWithCapacityOfOne {
    s_entriesCapacity = 1;
    XCTAssertTrue(0 == HashVnode(reinterpret_cast<vnode_t>(static_cast<uintptr_t>(1))));
}

@end
