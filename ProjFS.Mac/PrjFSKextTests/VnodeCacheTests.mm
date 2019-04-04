#import <XCTest/XCTest.h>

typedef int16_t VirtualizationRootHandle;

#include "MockVnodeAndMount.hpp"
#include "KextLogMock.h"
#include "KextMockUtilities.hpp"
#include "../PrjFSKext/VirtualizationRootsTestable.hpp"
#include "../PrjFSKext/VnodeCache.hpp"
#include "../PrjFSKext/VnodeCachePrivate.hpp"
#include "../PrjFSKext/VnodeCacheTestable.hpp"

using KextMock::_;
using std::shared_ptr;

@interface VnodeCacheTests : XCTestCase
@end

@implementation VnodeCacheTests
{
    shared_ptr<mount> testMount;
    shared_ptr<vnode> testVnode;
}

// Dummy vfs_context implementation for vfs_context_t
struct vfs_context
{
};

static const VirtualizationRootHandle TestRootHandle = 1;
static const VirtualizationRootHandle TestSecondRootHandle = 2;

static void AllocateCacheEntries(uint32_t capacity, bool fillCache);
static void FreeCacheEntries();
static void MarkEntryAsFree(uintptr_t entryIndex);

- (void) setUp
{
    kern_return_t initResult = VirtualizationRoots_Init();
    XCTAssertEqual(initResult, KERN_SUCCESS);
    
    testMount = mount::Create();
    testVnode = testMount->CreateVnodeTree("/Users/test/code/Repo/file");
}

- (void) tearDown
{
    testVnode.reset();
    testMount.reset();
    MockCalls::Clear();
    
    VirtualizationRoots_Cleanup();
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
    shared_ptr<vnode> testVnode2 = testMount->CreateVnodeTree("/Users/test/code/Repo/file2");
    shared_ptr<vnode> testVnode3 = testMount->CreateVnodeTree("/Users/test/code/Repo/file3");
    
    XCTAssertTrue(0 == ComputeVnodeHashIndex(testVnode.get()));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(testVnode2.get()));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(testVnode3.get()));
}

- (void)testVnodeCache_InvalidateCache_SetsMemoryToZeros {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    PerfTracer dummyPerfTracer;
    VnodeCache_InvalidateCache(&dummyPerfTracer);
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
    
    free(emptyArray);
    FreeCacheEntries();
}

- (void)testInvalidateCache_ExclusiveLocked_SetsMemoryToZeros {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    InvalidateCache_ExclusiveLocked();
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
    
    free(emptyArray);
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_ReturnsStartingIndexWhenNull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode.get(), startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == startingIndex);
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_ReturnsFalseWhenCacheFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertFalse(TryFindVnodeIndex_Locked(testVnode.get(), startingIndex, /* out */ cacheIndex));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_WrapsToBeginningWhenResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t emptyIndex = 2;
    MarkEntryAsFree(emptyIndex);
    
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode.get(), startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
    
    FreeCacheEntries();
}

- (void)testTryFindVnodeIndex_Locked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    uintptr_t emptyIndex = s_entriesCapacity - 1;
    MarkEntryAsFree(emptyIndex);
    
    uintptr_t startingIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(testVnode.get(), startingIndex, /* out */ cacheIndex));
    XCTAssertTrue(emptyIndex == cacheIndex);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
    
    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReturnsFalseWhenFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);

    uintptr_t indexFromHash = 5;
    uint32_t testVnodeVid = 7;

    XCTAssertFalse(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            TestRootHandle));

    XCTAssertFalse(MockCalls::DidCallAnyFunctions());

    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesIndeterminateEntry {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testVnode.get());
    uint32_t testVnodeVid = testVnode->GetVid();

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            RootHandle_Indeterminate));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);

    PerfTracer dummyPerfTracer;
    vfs_context dummyContext;
    
    XCTAssertTrue(TestRootHandle == VnodeCache_FindRootForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testVnode.get(),
        &dummyContext));

    XCTAssertFalse(MockCalls::DidCallAnyFunctions());

    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesEntryAfterRecyclingVnode {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testVnode.get());
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnode->GetVid(),
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnode->GetVid() == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    testVnode->StartRecycling();
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnode->GetVid(),
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(testVnode->GetVid() == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestSecondRootHandle == s_entries[indexFromHash].virtualizationRoot);

    PerfTracer dummyPerfTracer;
    vfs_context dummyContext;
    
    XCTAssertTrue(TestSecondRootHandle == VnodeCache_FindRootForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testVnode.get(),
        &dummyContext));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());

    FreeCacheEntries();
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_LogsErrorWhenCacheHasDifferentRoot {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testVnode.get());
    uint32_t testVnodeVid = 7;

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);

    XCTAssertTrue(MockCalls::DidCallFunction(KextMessageLogged, KEXTLOG_ERROR));

    FreeCacheEntries();
}

// VnodeCache_FindRootForVnode
// TryGetVnodeRootFromCache

- (void)testVnodeCache_RefreshRootForVnode {
    vfs_context dummyContext;
    PerfTracer dummyPerfTracer;
    const char* repoPath = "/Users/test/code/Repo2";
    const char* filePath = "/Users/test/code/Repo2/file";
    const char* deeplyNestedPath = "/Users/test/code/Repo2/deeply/nested/sub/directories/with/a/file";

    shared_ptr<vnode> repoRootVnode = self->testMount->CreateVnodeTree(repoPath, VDIR);
    shared_ptr<vnode> testFileVnode = self->testMount->CreateVnodeTree(filePath);
    shared_ptr<vnode> deepFileVnode = self->testMount->CreateVnodeTree(deeplyNestedPath);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */, 0,
        repoRootVnode.get(),
        repoRootVnode->GetVid(),
        FsidInode{ repoRootVnode->GetMountPoint()->GetFsid(), repoRootVnode->GetInode() },
        repoPath);
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testFileVnode.get(),
        &dummyContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    // Initialize the cache
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testFileVnode.get());
    uint32_t testVnodeVid = testFileVnode->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testFileVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // VnodeCache_RefreshRootForVnode should
    // force a lookup of the new root and set it in the cache
    VirtualizationRootHandle rootHandle = VnodeCache_RefreshRootForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testFileVnode.get(),
        &dummyContext);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(rootHandle == s_entries[indexFromHash].virtualizationRoot);
}

- (void)testVnodeCache_InvalidateVnodeAndGetLatestRoot {
    vfs_context dummyContext;
    PerfTracer dummyPerfTracer;
    const char* repoPath = "/Users/test/code/Repo2";
    const char* filePath = "/Users/test/code/Repo2/file";
    const char* deeplyNestedPath = "/Users/test/code/Repo2/deeply/nested/sub/directories/with/a/file";

    shared_ptr<vnode> repoRootVnode = self->testMount->CreateVnodeTree(repoPath, VDIR);
    shared_ptr<vnode> testFileVnode = self->testMount->CreateVnodeTree(filePath);
    shared_ptr<vnode> deepFileVnode = self->testMount->CreateVnodeTree(deeplyNestedPath);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */, 0,
        repoRootVnode.get(),
        repoRootVnode->GetVid(),
        FsidInode{ repoRootVnode->GetMountPoint()->GetFsid(), repoRootVnode->GetInode() },
        repoPath);
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testFileVnode.get(),
        &dummyContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    // Initialize the cache
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testFileVnode.get());
    uint32_t testVnodeVid = testFileVnode->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testFileVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // VnodeCache_InvalidateVnodeAndGetLatestRoot should return the real root and
    // set the entry in the cache to RootHandle_Indeterminate
    VirtualizationRootHandle rootHandle = VnodeCache_InvalidateVnodeAndGetLatestRoot(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testFileVnode.get(),
        &dummyContext);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
}

- (void)testLookupVnodeRootAndUpdateCache_RefreshAndInvalidateEntry {
    vfs_context dummyContext;
    PerfTracer dummyPerfTracer;
    const char* repoPath = "/Users/test/code/Repo2";
    const char* filePath = "/Users/test/code/Repo2/file";
    const char* deeplyNestedPath = "/Users/test/code/Repo2/deeply/nested/sub/directories/with/a/file";

    shared_ptr<vnode> repoRootVnode = self->testMount->CreateVnodeTree(repoPath, VDIR);
    shared_ptr<vnode> testFileVnode = self->testMount->CreateVnodeTree(filePath);
    shared_ptr<vnode> deepFileVnode = self->testMount->CreateVnodeTree(deeplyNestedPath);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */, 0,
        repoRootVnode.get(),
        repoRootVnode->GetVid(),
        FsidInode{ repoRootVnode->GetMountPoint()->GetFsid(), repoRootVnode->GetInode() },
        repoPath);
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        testFileVnode.get(),
        &dummyContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    // Initialize the cache
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(testFileVnode.get());
    uint32_t testVnodeVid = testFileVnode->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            testFileVnode.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // LookupVnodeRootAndUpdateCache with UpdateCacheBehavior_ForceRefresh should
    // force a lookup of the new root and set it in the cache
    VirtualizationRootHandle rootHandle;
    LookupVnodeRootAndUpdateCache(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        &dummyContext,
        testFileVnode.get(),
        indexFromHash,
        testVnodeVid,
        UpdateCacheBehavior_ForceRefresh,
        /* out parameters */
        rootHandle);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(rootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // UpdateCacheBehavior_InvalidateEntry means that the root in the cache should be
    // set to RootHandle_Indeterminate, but the real root will still be returned
    LookupVnodeRootAndUpdateCache(
        &dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        &dummyContext,
        testFileVnode.get(),
        indexFromHash,
        testVnodeVid,
        UpdateCacheBehavior_InvalidateEntry,
        /* out parameters */
        rootHandle);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
}

static void AllocateCacheEntries(uint32_t capacity, bool fillCache)
{
    s_entriesCapacity = capacity;
    s_entries = new VnodeCacheEntry[s_entriesCapacity];
    
    static shared_ptr<mount> dummyMount = mount::Create();
    static shared_ptr<vnode> dummyNode = dummyMount->CreateVnodeTree("/DUMMY");
    for (uint32_t i = 0; i < s_entriesCapacity; ++i)
    {
        if (fillCache)
        {
            s_entries[i].vnode = dummyNode.get();
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
