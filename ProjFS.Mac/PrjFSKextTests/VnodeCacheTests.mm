#import <XCTest/XCTest.h>
#include "MockVnodeAndMount.hpp"
#include "KextLogMock.h"
#include "KextMockUtilities.hpp"
#include "VnodeCacheEntriesWrapper.hpp"
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

static const VirtualizationRootHandle DummyRootHandle = 51;
static const VirtualizationRootHandle DummyRootHandleTwo = 52;

- (void)setUp
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

- (void)tearDown
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
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));
    
    // We don't care which mocks were called during the initialization code (above)
    MockCalls::Clear();
    
    XCTAssertTrue(repoRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_FindRootForVnode_FullCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));
    
    // We don't care which mocks were called during the initialization code (above)
    MockCalls::Clear();
    
    XCTAssertTrue(repoRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_FindRootForVnode_VnodeInCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    VirtualizationRootHandle repoRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(repoRootHandle));
    
    // We don't care which mocks were called during the initialization code (above)
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
    
    // Sanity check: We don't expect any of the mock functions to have been called
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

    // We don't care which mocks were called during the initialization code (above)
    MockCalls::Clear();

    // Initialize the cache
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    // Insert testFileVnode with DummyRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_InvalidateVnodeRootAndGetLatestRoot {
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

    // We don't care which mocks were called during the initialization code (above)
    MockCalls::Clear();

    // Initialize the cache
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    // Insert testFileVnode with DummyRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    // VnodeCache_InvalidateVnodeRootAndGetLatestRoot should return the real root and
    // set the entry in the cache to RootHandle_Indeterminate
    VirtualizationRootHandle rootHandle = VnodeCache_InvalidateVnodeRootAndGetLatestRoot(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertTrue(rootHandle == repoRootHandle);
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == cacheWrapper[indexFromHash].virtualizationRoot);
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testVnodeCache_InvalidateCache_SetsMemoryToZeros {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);

    shared_ptr<VnodeCacheEntry> emptyArray(static_cast<VnodeCacheEntry*>(calloc(cacheWrapper.GetCapacity(), sizeof(VnodeCacheEntry))), free);
    XCTAssertTrue(0 != memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    VnodeCache_InvalidateCache(&self->dummyPerfTracer);
    XCTAssertTrue(0 == memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testInvalidateCache_ExclusiveLocked_SetsMemoryToZeros {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);
    
    shared_ptr<VnodeCacheEntry> emptyArray(static_cast<VnodeCacheEntry*>(calloc(cacheWrapper.GetCapacity(), sizeof(VnodeCacheEntry))), free);
    XCTAssertTrue(0 != memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    InvalidateCache_ExclusiveLocked();
    XCTAssertTrue(0 == memcmp(emptyArray.get(), s_entries, sizeof(VnodeCacheEntry) * cacheWrapper.GetCapacity()));
    
    // Sanity check: We don't expect any of the mock functions to have been called
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
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryGetVnodeRootFromCache_VnodeInCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    uintptr_t testIndex = 5;
    cacheWrapper[testIndex].vnode = self->testVnodeFile1.get();
    cacheWrapper[testIndex].vid = self->testVnodeFile1->GetVid();
    cacheWrapper[testIndex].virtualizationRoot = DummyRootHandle;
    
    VirtualizationRootHandle rootHandle = 1;
    XCTAssertTrue(
        TryGetVnodeRootFromCache(
            self->testVnodeFile1.get(),
            testIndex,
            self->testVnodeFile1->GetVid(),
            rootHandle));
    XCTAssertTrue(DummyRootHandle == rootHandle);
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryGetVnodeRootFromCache_VnodeNotInCache {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    VirtualizationRootHandle rootHandle = 1;
    XCTAssertFalse(
        TryGetVnodeRootFromCache(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            self->testVnodeFile1->GetVid(),
            rootHandle));
    XCTAssertTrue(RootHandle_None == rootHandle);
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testInsertEntryToInvalidatedCache_ExclusiveLocked_CacheFull {
    // In production InsertEntryToInvalidatedCache_ExclusiveLocked should never
    // be called when the cache is full, but by this test doing so we can validate
    // that InsertEntryToInvalidatedCache_ExclusiveLocked logs an error if it fails to
    // insert a vnode into the cache
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);
    
    InsertEntryToInvalidatedCache_ExclusiveLocked(
        self->testVnodeFile1.get(),
        ComputeVnodeHashIndex(self->testVnodeFile1.get()),
        self->testVnodeFile1->GetVid(),
        DummyRootHandle);
    
    XCTAssertTrue(MockCalls::DidCallFunction(KextMessageLogged, KEXTLOG_ERROR));
}

- (void)testFindVnodeRootFromDiskAndUpdateCache_RefreshAndInvalidateEntry {
    VirtualizationRootHandle onDiskRootHandle = InsertVirtualizationRoot_Locked(
        nullptr /* no client */,
        0,
        self->repoRootVnode.get(),
        self->repoRootVnode->GetVid(),
        FsidInode{ self->repoRootVnode->GetMountPoint()->GetFsid(), self->repoRootVnode->GetInode() },
        self->repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(onDiskRootHandle));
    XCTAssertTrue(DummyRootHandle != onDiskRootHandle);

    VirtualizationRootHandle foundRoot = VirtualizationRoot_FindForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext);
    XCTAssertEqual(foundRoot, onDiskRootHandle);

    // We don't care which mocks were called during the initialization code (above)
    MockCalls::Clear();

    // Initialize the cache
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    // Insert testFileVnode with DummyRootHandle as its root
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
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
    XCTAssertTrue(rootHandle == onDiskRootHandle);
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(rootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);

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
    XCTAssertTrue(rootHandle == onDiskRootHandle);
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(RootHandle_Indeterminate == cacheWrapper[indexFromHash].virtualizationRoot);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    
    // Sanity check: We don't expect any of the mock functions to have been called
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

    // We don't care which mocks were called during the initialization code (above)
    MockCalls::Clear();

    // Initialize the cache
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);
    
    // Insert testFileVnode with DummyRootHandle as its root
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
    
    for (uintptr_t index = 0; index < cacheWrapper.GetCapacity(); ++index)
    {
        if (index == indexFromHash)
        {
            XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
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
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testFindVnodeRootFromDiskAndUpdateCache_InvalidUpdateCacheBehaviorValue {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();
    
    // This test is to ensure 100% coverage of FindVnodeRootFromDiskAndUpdateCache
    // In production FindVnodeRootFromDiskAndUpdateCache would panic when it sees an
    // invalid UpdateCacheBehavior, but in user-mode the assertf if a no-op
    VirtualizationRootHandle rootHandle;
    FindVnodeRootFromDiskAndUpdateCache(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        self->dummyVFSContext,
        self->testVnodeFile1.get(),
        indexFromHash,
        testVnodeVid,
        UpdateCacheBehavior_Invalid,
        /* out parameters */
        rootHandle);
}

- (void)testTryFindVnodeIndex_Locked_ReturnsVnodeHashIndexWhenSlotEmpty {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t cacheIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ cacheIndex));
    XCTAssertTrue(cacheIndex == vnodeHashIndex);
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsFalseWhenCacheFull {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);
    
    uintptr_t vnodeIndex;
    XCTAssertFalse(
        TryFindVnodeIndex_Locked(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            /* out */ vnodeIndex));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_WrapsToBeginningWhenResolvingCollisions {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);
    
    uintptr_t emptyIndex = 2;
    cacheWrapper.MarkEntryAsFree(emptyIndex);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t vnodeIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ vnodeIndex));
    XCTAssertTrue(emptyIndex == vnodeIndex);
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryFindVnodeIndex_Locked_ReturnsLastIndexWhenEmptyAndResolvingCollisions {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);
    uintptr_t emptyIndex = cacheWrapper.GetCapacity() - 1;
    cacheWrapper.MarkEntryAsFree(emptyIndex);
    
    uintptr_t vnodeHashIndex = 5;
    uintptr_t vnodeIndex;
    XCTAssertTrue(TryFindVnodeIndex_Locked(self->testVnodeFile1.get(), vnodeHashIndex, /* out */ vnodeIndex));
    XCTAssertTrue(emptyIndex == vnodeIndex);

    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReturnsFalseWhenFull {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ true);

    XCTAssertFalse(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            ComputeVnodeHashIndex(self->testVnodeFile1.get()),
            self->testVnodeFile1->GetVid(),
            true, // forceRefreshEntry
            DummyRootHandle));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesIndeterminateEntry {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    uint32_t testVnodeVid = self->testVnodeFile1->GetVid();

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            true, // forceRefreshEntry
            RootHandle_Indeterminate));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(RootHandle_Indeterminate == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            testVnodeVid,
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(testVnodeVid == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(DummyRootHandle == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_ReplacesEntryAfterRecyclingVnode {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    self->testVnodeFile1->StartRecycling();
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            DummyRootHandleTwo));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandleTwo == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(DummyRootHandleTwo == VnodeCache_FindRootForVnode(
        &self->dummyPerfTracer,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Hit,
        PrjFSPerfCounter_VnodeOp_Vnode_Cache_Miss,
        PrjFSPerfCounter_VnodeOp_FindRoot,
        PrjFSPerfCounter_VnodeOp_FindRoot_Iteration,
        self->testVnodeFile1.get(),
        self->dummyVFSContext));
    
    // Sanity check: We don't expect any of the mock functions to have been called
    XCTAssertFalse(MockCalls::DidCallAnyFunctions());
}

- (void)testTryInsertOrUpdateEntry_ExclusiveLocked_LogsErrorWhenCacheHasDifferentRoot {
    VnodeCacheEntriesWrapper cacheWrapper(/* fillCache*/ false);
    uintptr_t indexFromHash = ComputeVnodeHashIndex(self->testVnodeFile1.get());

    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            DummyRootHandle));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(
        TryInsertOrUpdateEntry_ExclusiveLocked(
            self->testVnodeFile1.get(),
            indexFromHash,
            self->testVnodeFile1->GetVid(),
            false, // forceRefreshEntry
            DummyRootHandleTwo));
    XCTAssertTrue(self->testVnodeFile1.get() == cacheWrapper[indexFromHash].vnode);
    XCTAssertTrue(self->testVnodeFile1->GetVid() == cacheWrapper[indexFromHash].vid);
    XCTAssertTrue(DummyRootHandle == cacheWrapper[indexFromHash].virtualizationRoot);
    
    XCTAssertTrue(MockCalls::DidCallFunction(KextMessageLogged, KEXTLOG_ERROR));
}

@end
