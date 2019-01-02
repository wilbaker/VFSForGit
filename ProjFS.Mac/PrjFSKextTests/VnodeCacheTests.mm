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

static const VirtualizationRootHandle TestRootHandle = 51;
static const VirtualizationRootHandle TestSecondRootHandle = 52;

// Helper class for interacting with s_entries and s_entriesCapacity
class VnodeCacheEntriesWrapper
{
public:
    VnodeCacheEntriesWrapper(const uint32_t capacity, const bool fillCache)
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
    
    ~VnodeCacheEntriesWrapper()
    {
        s_entriesCapacity = 0;
        delete[] s_entries;
    }
    
    void MarkEntryAsFree(const uintptr_t entryIndex)
    {
        s_entries[entryIndex].vnode = nullptr;
    }
    
    VnodeCacheEntry& operator[] (const uintptr_t entryIndex)
    {
        return s_entries[entryIndex];
    }
    
    uint32_t GetCapacity() const
    {
        return s_entriesCapacity;
    }
};

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

- (void)testVnodeCache_FindRootForVnode_EmptyCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));
    
    MockCalls::Clear();
    
    XCTAssertTrue(repoRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_FindRootForVnode_FullCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));
    
    MockCalls::Clear();
    
    XCTAssertTrue(repoRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_FindRootForVnode_VnodeInCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));
    
    MockCalls::Clear();
    
    // The first call to VnodeCache_FindRootForVnode results in the vnode being added to the cache
    XCTAssertTrue(repoRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    uintptr_t vnodeIndex = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[vnodeIndex].vnode);
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[vnodeIndex].vid);
    XCTAssertTrue(repoRootHandle == cacheWrapper[vnodeIndex].virtualizationRoot);
    
    XCTAssertTrue(repoRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
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
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(rootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_InvalidateCache_SetsMemoryToZeros {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ true);

    shared_ptr<VnodeCacheEntry> emptyArray(static_cast<VnodeCacheEntry*>(calloc(cacheWrapper.GetCapacity(), sizeof(VnodeCacheEntry))), free);
    XCTAssertTrue(0 != memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    VnodeCache_InvalidateCache(&self->dummyPerfTracer);
    XCTAssertTrue(0 == memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testInvalidateCache_ExclusiveLocked_SetsMemoryToZeros {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ true);
    
    shared_ptr<VnodeCacheEntry> emptyArray(static_cast<VnodeCacheEntry*>(calloc(cacheWrapper.GetCapacity(), sizeof(VnodeCacheEntry))), free);
    XCTAssertTrue(0 != memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    InvalidateCache_ExclusiveLocked();
    XCTAssertTrue(0 == memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
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
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 1, /* fillCache*/ false);
    XCTAssertTrue(0 == ComputeVnodeHashIndex(self->testVnodeFile1.get()));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(self->testVnodeFile2.get()));
    XCTAssertTrue(0 == ComputeVnodeHashIndex(self->testVnodeFile3.get()));
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryGetVnodeRootFromCache_VnodeInCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
    uintptr_t testIndex = 5;
    cacheWrapper[testIndex].vnode = self->testVnodeFile1.get();
    cacheWrapper[testIndex].vid = self->testVnodeFile1->GetVid();
    cacheWrapper[testIndex].virtualizationRoot = TestRootHandle;
    
    VirtualizationRootHandle rootHandle = 1;
    XCTAssertTrue(
        TryGetVnodeRootFromCache(
            self->testVnodeFile1.get(),
            testIndex,
            self->testVnodeFile1->GetVid(),
            rootHandle));
    XCTAssertTrue(TestRootHandle == rootHandle);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryGetVnodeRootFromCache_VnodeNotInCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
    VirtualizationRootHandle rootHandle = 1;
    XCTAssertFalse(
        TryGetVnodeRootFromCache(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            self->testVnodeFile1->GetVid(),
            rootHandle));
    XCTAssertTrue(RootHandle_None == rootHandle);
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
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(rootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    VnodeCacheEntriesWrapper cacheWrapper(cacheCapacity, /* fillCache*/ true);
    
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
            XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
            XCTAssertTrue(rootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
        }
        else
        {
            XCTAssertTrue(nullptr == cacheWrapper[index].vnode);
            XCTAssertTrue(0 == cacheWrapper[index].vid);
            XCTAssertTrue(0 == cacheWrapper[index].virtualizationRoot);
        }
    }
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsVnodeHashIndexWhenSlotEmpty {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == vnodeHashIndex);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsFalseWhenCacheFull {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t vnodeIndex;
    XCTAssertFalse(
        TryFindVnodeIndex_Locked(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            /* out */ vnodeIndex));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_WrapsToBeginningWhenResolvingCollisions {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ true);
    
    uintptr_t emptyIndex = 2;
    cacheWrapper.MarkEntryAsFree(emptyIndex);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t vnodeIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ vnodeIndex));
    XCTAssertTrue(emptyIndex == vnodeIndex);
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ true);
    uintptr_t emptyIndex = cacheWrapper.GetCapacity() - 1;
    cacheWrapper.MarkEntryAsFree(emptyIndex);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t vnodeIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ vnodeIndex));
    XCTAssertTrue(emptyIndex == vnodeIndex);

    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReturnsFalseWhenFull {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ true);

    XCTAssertFalse(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            self->testVnodeFile1->GetVid(),
            true, // forceRefreshEntry
            TestRootHandle));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesIndeterminateEntry {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            RootHandle_Indeterminate));
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(TestRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesEntryAfterRecyclingVnode {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    self->testVnodeFile1->StartRecycling();
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestSecondRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(TestSecondRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_LogsErrorWhenCacheHasDifferentRoot {
    VnodeCacheEntriesWrapper cacheWrapper(/* capacity*/ 100, /* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            TestSecondRootHandle));
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(TestRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(MockCalls::DidCallFunction(KextMessageLogged, KEXTLOG_ERROR));
}

@end
