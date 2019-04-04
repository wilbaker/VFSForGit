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
    std::string repoPath;
    shared_ptr<mount> testMount;
    shared_ptr<vnode> repoRootVnode;
    shared_ptr<vnode> testVnodeFile1;
    shared_ptr<vnode> testVnodeFile2;
    shared_ptr<vnode> testVnodeFile3;
    PerfTracer dummyPerfTracer;
    vfs_context_t dummyVFSContext;
}

static const VirtualizationRootHandle TestRootHandle = 1;
static const VirtualizationRootHandle TestSecondRootHandle = 2;

static void AllocateCacheEntries(uint32_t capacity, bool fillCache);
static void FreeCacheEntries();
static void MarkEntryAsFree(uintptr_t entryIndex);

- (void) setUp
{
    kern_return_t initResult = VirtualizationRoots_Init();
    XCTAssertEqual(initResult, KERN_SUCCESS);
    
    self->testMount = mount::Create();
    repoPath = "/Users/test/code/Repo";
    self->repoRootVnode = self->testMount->CreateVnodeTree(repoPath, VDIR);
    self->testVnodeFile1 = testMount->CreateVnodeTree(repoPath + "/file1");
    self->testVnodeFile2 = testMount->CreateVnodeTree(repoPath + "/file2");
    self->testVnodeFile3 = testMount->CreateVnodeTree(repoPath + "/file3");
    self->dummyVFSContext = vfs_context_create(nullptr);
}

- (void) tearDown
{
    vfs_context_rele(self->dummyVFSContext);
    self->testVnodeFile3.reset();
    self->testVnodeFile2.reset();
    self->testVnodeFile1.reset();
    self->repoRootVnode.reset();
    self->testMount.reset();
    MockCalls::Clear();
    VirtualizationRoots_Cleanup();
}

- (void)testVnodeCache_FindRootForVnodeEmptyCache {

}

- (void)testVnodeCache_FindRootForVnodeFullCache {

}

- (void)testVnodeCache_FindRootForVnodeVnodeInCache {

}

- (void)testVnodeCache_RefreshRootForVnode {
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    MockCalls::Clear();

    // Initialize the cache
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // VnodeCache_RefreshRootForVnode should
    // force a lookup of the new root and set it in the cache
    VirtualizationRootHandle rootHandle = VnodeCache_RefreshRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(rootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_InvalidateVnodeAndGetLatestRoot {
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    MockCalls::Clear();

    // Initialize the cache
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // VnodeCache_InvalidateVnodeAndGetLatestRoot should return the real root and
    // set the entry in the cache to RootHandle_Indeterminate
    VirtualizationRootHandle rootHandle = VnodeCache_InvalidateVnodeAndGetLatestRoot(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_InvalidateCache_SetsMemoryToZeros {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);

    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    VnodeCache_InvalidateCache(&self->dummyPerfTracer);
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    free(emptyArray);

    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testInvalidateCache_ExclusiveLocked_SetsMemoryToZeros {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    VnodeCacheEntry* emptyArray = static_cast<VnodeCacheEntry*>(calloc(s_entriesCapacity, sizeof(VnodeCacheEntry)));
    XCTAssertTrue(0 != memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry) * s_entriesCapacity));
    
    InvalidateCache_ExclusiveLocked();
    XCTAssertTrue(0 == memcmp(emptyArray, s_entries, sizeof(VnodeCacheEntry)*s_entriesCapacity));
    free(emptyArray);
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testComputePow2CacheCapacity {

    // At a minimum ComputePow2CacheCapacity should return the minimum value in AllowedPow2CacheCapacities
    XCTAssertTrue(MinPow2VnodeCacheCapacity == ComputePow2CacheCapacity(0));
    
    // ComputePow2CacheCapacity should round up to the nearest power of 2 (after multiplying expectedVnodeCount by 2)
    int expectedVnodeCount = MinPow2VnodeCacheCapacity/2 + 1;
    XCTAssertTrue(MinPow2VnodeCacheCapacity << 1 == ComputePow2CacheCapacity(expectedVnodeCount));
    
    // ComputePow2CacheCapacity should be capped at the maximum value in AllowedPow2CacheCapacities
    XCTAssertTrue(MaxPow2VnodeCacheCapacity == ComputePow2CacheCapacity(MaxPow2VnodeCacheCapacity + 1));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testComputeVnodeHashKeyWithCapacityOfOne {
    s_entriesCapacity = 1;
    XCTAssertTrue(0 == ComputeVnodeHashIndex(self->testVnodeFile1.get()));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(self->testVnodeFile2.get()));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(self->testVnodeFile3.get()));
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryGetVnodeRootFromCache_VnodeInCache {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    uintptr_t testIndex = 5;
    s_entries[testIndex].vnode = self->testVnodeFile1.get();
    s_entries[testIndex].vid = self->testVnodeFile1->GetVid();
    s_entries[testIndex].virtualizationRoot = TestRootHandle;
    
    VirtualizationRootHandle rootHandle = 1;
    XCTAssertTrue(
        TryGetVnodeRootFromCache(
            self->testVnodeFile1.get(),
            testIndex,
            self->testVnodeFile1->GetVid(),
            rootHandle));
    XCTAssertTrue(TestRootHandle == rootHandle);
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryGetVnodeRootFromCache_VnodeNotInCache {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    VirtualizationRootHandle rootHandle = 1;
    XCTAssertFalse(
        TryGetVnodeRootFromCache(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            self->testVnodeFile1->GetVid(),
            rootHandle));
    XCTAssertTrue(RootHandle_None == rootHandle);
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testFindVnodeRootFromDiskAndUpdateCache_RefreshAndInvalidateEntry {
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    MockCalls::Clear();

    // Initialize the cache
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    // FindVnodeRootFromDiskAndUpdateCache with UpdateCacheBehavior_ForceRefresh should
    // force a lookup of the new root and set it in the cache
    VirtualizationRootHandle rootHandle;
    FindVnodeRootFromDiskAndUpdateCache(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        self->dummyVFSContext,
        self->testVnodeFile1.get(),
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
    FindVnodeRootFromDiskAndUpdateCache(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        self->dummyVFSContext,
        self->testVnodeFile1.get(),
        indexFromHash,
        testVnodeVid,
        UpdateCacheBehavior_InvalidateEntry,
        /* out parameters */
        rootHandle);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testFindVnodeRootFromDiskAndUpdateCache_FullCache {
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertEqual(foundRoot, repoRootHandle);

    MockCalls::Clear();

    // Initialize the cache
    uint32_t cacheCapacity = 100;
    AllocateCacheEntries(cacheCapacity, /* fillCache*/ true);
    
    // Insert testFileVnode with TestRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    
    // UpdateCacheBehavior_TrustCurrentEntry will use the current entry if present
    // In this case there is no entry for the vnode and so the cache will be invalidated
    // and a new entry added
    VirtualizationRootHandle rootHandle;
    FindVnodeRootFromDiskAndUpdateCache(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        self->dummyVFSContext,
        self->testVnodeFile1.get(),
        indexFromHash,
        testVnodeVid,
        UpdateCacheBehavior_TrustCurrentEntry,
        /* out parameters */
        rootHandle);
    XCTAssertTrue(rootHandle == repoRootHandle);
    
    for (uintptr_t index = 0; index < cacheCapacity; ++index)
    {
        if (index == indexFromHash)
        {
            XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
            XCTAssertTrue(rootHandle == s_entries[indexFromHash].virtualizationRoot);
        }
        else
        {
            XCTAssertTrue(nullptr == s_entries[index].vnode);
            XCTAssertTrue(0 == s_entries[index].vid);
            XCTAssertTrue(0 == s_entries[index].virtualizationRoot);
        }
    }
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsVnodeHashIndexWhenSlotEmpty {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == vnodeHashIndex);
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsFalseWhenCacheFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t vnodeIndex;
    XCTAssertFalse(
        TryFindVnodeIndex_Locked(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            /* out */ vnodeIndex));
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_WrapsToBeginningWhenResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t emptyIndex = 2;
    MarkEntryAsFree(emptyIndex);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t vnodeIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ vnodeIndex));
    XCTAssertTrue(emptyIndex == vnodeIndex);
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);
    uintptr_t emptyIndex = s_entriesCapacity - 1;
    MarkEntryAsFree(emptyIndex);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t vnodeIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ vnodeIndex));
    XCTAssertTrue(emptyIndex == vnodeIndex);
    
    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReturnsFalseWhenFull {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ true);

    XCTAssertFalse(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            self->testVnodeFile1->GetVid(),
            true, // forceRefreshEntry
            TestRootHandle));

    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesIndeterminateEntry {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            RootHandle_Indeterminate));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(TestRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));

    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesEntryAfterRecyclingVnode {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    self->testVnodeFile1->StartRecycling();
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestSecondRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(TestSecondRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));

    FreeCacheEntries();
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_LogsErrorWhenCacheHasDifferentRoot {
    AllocateCacheEntries(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == s_entries[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == s_entries[indexFromHash].virtualizationRoot);

    FreeCacheEntries();
    
    XCTAssertTrue(MockCalls::DidCallFunction(KextMessageLogged, KEXTLOG_ERROR));
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
