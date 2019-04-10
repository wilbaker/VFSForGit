#include "../PrjFSKext/kernel-header-wrappers/vnode.h"
#include "../PrjFSKext/KauthHandlerTestable.hpp"
#include "../PrjFSKext/PerformanceTracing.hpp"
#include "../PrjFSKext/VirtualizationRootsTestable.hpp"
#import <XCTest/XCTest.h>
#import <sys/stat.h>
#include "MockVnodeAndMount.hpp"
#include "MockProc.hpp"
#include "VnodeCacheEntriesWrapper.hpp"

using std::shared_ptr;

// Dummy implementation
class org_vfsforgit_PrjFSProviderUserClient
{
};

@interface KauthHandlerTests : XCTestCase
@end

@implementation KauthHandlerTests
{
    VnodeCacheEntriesWrapper cacheWrapper;
}

- (void) setUp {
    kern_return_t initResult = VirtualizationRoots_Init();
    XCTAssertEqual(initResult, KERN_SUCCESS);
    self->cacheWrapper.AllocateCache(false /* fillCache */);
}

- (void) tearDown {
    self->cacheWrapper.FreeCache();
    MockVnodes_CheckAndClear();
    VirtualizationRoots_Cleanup();
}

- (void)testActionBitIsSet {
    XCTAssertTrue(ActionBitIsSet(KAUTH_VNODE_READ_DATA, KAUTH_VNODE_READ_DATA));
    XCTAssertTrue(ActionBitIsSet(KAUTH_VNODE_WRITE_DATA, KAUTH_VNODE_WRITE_DATA));
    XCTAssertTrue(ActionBitIsSet(KAUTH_VNODE_WRITE_DATA, KAUTH_VNODE_READ_DATA | KAUTH_VNODE_WRITE_DATA));
    XCTAssertTrue(ActionBitIsSet(KAUTH_VNODE_READ_DATA | KAUTH_VNODE_WRITE_DATA, KAUTH_VNODE_WRITE_DATA));
    XCTAssertFalse(ActionBitIsSet(KAUTH_VNODE_WRITE_DATA, KAUTH_VNODE_READ_DATA));
}

- (void)testIsFileSystemCrawler {
    XCTAssertTrue(IsFileSystemCrawler("mds"));
    XCTAssertTrue(IsFileSystemCrawler("mdworker"));
    XCTAssertTrue(IsFileSystemCrawler("mds_stores"));
    XCTAssertTrue(IsFileSystemCrawler("fseventsd"));
    XCTAssertTrue(IsFileSystemCrawler("Spotlight"));
    XCTAssertFalse(IsFileSystemCrawler("mds_"));
    XCTAssertFalse(IsFileSystemCrawler("spotlight"));
    XCTAssertFalse(IsFileSystemCrawler("git"));
}

- (void)testFileFlagsBitIsSet {
    XCTAssertTrue(FileFlagsBitIsSet(FileFlags_IsEmpty, FileFlags_IsEmpty));
    XCTAssertTrue(FileFlagsBitIsSet(FileFlags_IsInVirtualizationRoot, FileFlags_IsInVirtualizationRoot));
    XCTAssertFalse(FileFlagsBitIsSet(FileFlags_IsInVirtualizationRoot, FileFlags_IsEmpty));
    XCTAssertFalse(FileFlagsBitIsSet(FileFlags_IsInVirtualizationRoot, FileFlags_Invalid));
}

- (void)testShouldIgnoreVnodeType {
    shared_ptr<mount> testMount = mount::Create();
    shared_ptr<vnode> testVnode = testMount->CreateVnode("/foo");
    XCTAssertTrue(ShouldIgnoreVnodeType(VNON, testVnode.get()));
    XCTAssertTrue(ShouldIgnoreVnodeType(VBLK, testVnode.get()));
    XCTAssertTrue(ShouldIgnoreVnodeType(VCHR, testVnode.get()));
    XCTAssertTrue(ShouldIgnoreVnodeType(VSOCK, testVnode.get()));
    XCTAssertTrue(ShouldIgnoreVnodeType(VFIFO, testVnode.get()));
    XCTAssertTrue(ShouldIgnoreVnodeType(VBAD, testVnode.get()));
    XCTAssertFalse(ShouldIgnoreVnodeType(VREG, testVnode.get()));
    XCTAssertFalse(ShouldIgnoreVnodeType(VDIR, testVnode.get()));
    XCTAssertFalse(ShouldIgnoreVnodeType(VLNK, testVnode.get()));
    XCTAssertFalse(ShouldIgnoreVnodeType(VSTR, testVnode.get()));
    XCTAssertFalse(ShouldIgnoreVnodeType(VCPLX, testVnode.get()));
    XCTAssertFalse(ShouldIgnoreVnodeType(static_cast<vtype>(1000), testVnode.get()));
}

- (void)testFileFlaggedInRoot {
    bool fileFlaggedInRoot;
    shared_ptr<mount> testMount = mount::Create();
    shared_ptr<vnode> testVnode = vnode::Create(testMount, "/foo");
    vfs_context_t _Nonnull context = vfs_context_create(nullptr);
    
    testVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), context, &fileFlaggedInRoot));
    XCTAssertTrue(fileFlaggedInRoot);

    testVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot | UF_NODUMP;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), context, &fileFlaggedInRoot));
    XCTAssertTrue(fileFlaggedInRoot);

    testVnode->attrValues.va_flags = FileFlags_IsEmpty;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), context, &fileFlaggedInRoot));
    XCTAssertFalse(fileFlaggedInRoot);
    
    testVnode->attrValues.va_flags = FileFlags_Invalid;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), context, &fileFlaggedInRoot));
    XCTAssertFalse(fileFlaggedInRoot);
    
    testVnode->attrValues.va_flags = 0x00000100;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), context, &fileFlaggedInRoot));
    XCTAssertFalse(fileFlaggedInRoot);

    testVnode->errors.getattr = EBADF;
    XCTAssertFalse(TryGetFileIsFlaggedAsInRoot(testVnode.get(), context, &fileFlaggedInRoot));
}

- (void)testShouldHandleVnodeOpEvent {
    // In Parameters
    shared_ptr<mount> testMount = mount::Create();
    shared_ptr<vnode> testVnode = vnode::Create(testMount, "/foo");
    testVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    PerfTracer perfTracer;
    vfs_context_t _Nonnull context = vfs_context_create(nullptr);
    kauth_action_t action = KAUTH_VNODE_READ_DATA;
    
    // Out Parameters
    vtype vnodeType;
    uint32_t vnodeFileFlags;
    int pid;
    char procname[MAXCOMLEN + 1] = "";
    int kauthResult;
    int kauthError;

    
    // Test Success Case
    XCTAssertTrue(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);
    
    
    // Test KAUTH_VNODE_ACCESS is not handled
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            KAUTH_VNODE_ACCESS,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);

    // Test KAUTH_VNODE_ACCESS | KAUTH_VNODE_READ_DATA is not handled
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            KAUTH_VNODE_ACCESS | KAUTH_VNODE_READ_DATA,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);
    
    // Test invalid File System
    shared_ptr<mount> testMountNone = mount::Create("none", fsid_t{}, 0);
    shared_ptr<vnode> testVnodeNone = vnode::Create(testMountNone, "/none");
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnodeNone.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);
    
    
    // Test invalid VNODE Type
    shared_ptr<vnode> testVnodeInvalidType = vnode::Create(testMount, "/foo2", VNON);
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnodeInvalidType.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);

    
    // Test failure reading attr
    testVnode->errors.getattr = EBADF;
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DENY);
    // reset to valid value
    testVnode->errors.getattr = 0;

    
    // Test invalid file flag
    testVnode->attrValues.va_flags = FileFlags_IsEmpty;
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);
    // reset to valid value
    testVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    

    // Test with file crawler trying to populate an empty file
    testVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    SetProcName("mds");
    XCTAssertFalse(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DENY);

    
    // Test with finder trying to populate an empty file
    SetProcName("Finder");
    XCTAssertTrue(
        ShouldHandleVnodeOpEvent(
            &perfTracer,
            context,
            testVnode.get(),
            action,
            &vnodeType,
            &vnodeFileFlags,
            &pid,
            procname,
            &kauthResult,
            &kauthError));
    XCTAssertEqual(kauthResult, KAUTH_RESULT_DEFER);
}

- (void)testShouldHandleFileOpEvent {
    PerfTracer perfTracer;
    vfs_context_t _Nonnull context = vfs_context_create(nullptr);
    shared_ptr<mount> testMount = mount::Create();
    std::string repoPath = "/Users/test/code/Repo";
    shared_ptr<vnode> repoRootVnode = testMount->CreateVnodeTree(repoPath, VDIR);
    shared_ptr<vnode> testVnodeFile = testMount->CreateVnodeTree(repoPath + "/file.txt");
    shared_ptr<vnode> testVnodeDirectory = testMount->CreateVnodeTree(repoPath + "/directory", VDIR);
    
    org_vfsforgit_PrjFSProviderUserClient userClient;
    VirtualizationRootHandle testRootHandle = InsertVirtualizationRoot_Locked(
        &userClient,
        0,
        repoRootVnode.get(),
        repoRootVnode->GetVid(),
        FsidInode{ repoRootVnode->GetMountPoint()->GetFsid(), repoRootVnode->GetInode() },
        repoPath.c_str());
    XCTAssertTrue(VirtualizationRoot_IsValidRootHandle(testRootHandle));

    VirtualizationRootHandle rootHandle;
    FsidInode vnodeFsidInode;
    int pid;
    
    // KAUTH_FILEOP_OPEN
    XCTAssertTrue(
        ShouldHandleFileOpEvent(
            &perfTracer,
            context,
            testVnodeFile.get(),
            KAUTH_FILEOP_OPEN,
            false, // isDirectory,
            &rootHandle,
            &vnodeFsidInode,
            &pid));
    XCTAssertTrue(rootHandle == testRootHandle);
    // Finding the root should have added testVnodeFile to the cache
    XCTAssertTrue(testVnodeFile.get() == self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].vnode);
    XCTAssertTrue(rootHandle == self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].virtualizationRoot);
    
    // KAUTH_FILEOP_LINK
    XCTAssertTrue(
        ShouldHandleFileOpEvent(
            &perfTracer,
            context,
            testVnodeFile.get(),
            KAUTH_FILEOP_LINK,
            false, // isDirectory,
            &rootHandle,
            &vnodeFsidInode,
            &pid));
    XCTAssertTrue(rootHandle == testRootHandle);
    // KAUTH_FILEOP_LINK should invalidate the cache entry for testVnodeFile
    XCTAssertTrue(testVnodeFile.get() == self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].vnode);
    XCTAssertTrue(RootHandle_Indeterminate == self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].virtualizationRoot);
    
    // KAUTH_FILEOP_RENAME (file)
    // Set a different value in the cache for testVnodeFile's root to validate that the cache is refreshed for renames
    self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].virtualizationRoot = rootHandle + 1;
    XCTAssertTrue(
        ShouldHandleFileOpEvent(
            &perfTracer,
            context,
            testVnodeFile.get(),
            KAUTH_FILEOP_RENAME,
            false, // isDirectory,
            &rootHandle,
            &vnodeFsidInode,
            &pid));
    XCTAssertTrue(rootHandle == testRootHandle);
    // The cache should have been refreshed for KAUTH_FILEOP_RENAME
    XCTAssertTrue(testVnodeFile.get() == self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].vnode);
    XCTAssertTrue(rootHandle == self->cacheWrapper[ComputeVnodeHashIndex(testVnodeFile.get())].virtualizationRoot);
    
    // KAUTH_FILEOP_RENAME (directory)
    // Directory KAUTH_FILEOP_RENAME events should invalidate the entire cache and then insert
    // only the directory vnode into the cache
    self->cacheWrapper.FillAllEntries();
    XCTAssertTrue(
        ShouldHandleFileOpEvent(
            &perfTracer,
            context,
            testVnodeDirectory.get(),
            KAUTH_FILEOP_RENAME,
            true, // isDirectory,
            &rootHandle,
            &vnodeFsidInode,
            &pid));
    XCTAssertTrue(rootHandle == testRootHandle);
    
    // Validate the cache is empty except for the testVnodeDirectory entry
    uintptr_t directoryVnodeHash = ComputeVnodeHashIndex(testVnodeDirectory.get());
    for (uintptr_t index = 0; index < self->cacheWrapper.GetCapacity(); ++index)
    {
        if (index == directoryVnodeHash)
        {
            XCTAssertTrue(testVnodeDirectory.get() == self->cacheWrapper[directoryVnodeHash].vnode);
            XCTAssertTrue(testVnodeDirectory->GetVid() == self->cacheWrapper[directoryVnodeHash].vid);
            XCTAssertTrue(rootHandle == self->cacheWrapper[directoryVnodeHash].virtualizationRoot);
        }
        else
        {
            XCTAssertTrue(nullptr == self->cacheWrapper[index].vnode);
            XCTAssertTrue(0 == self->cacheWrapper[index].vid);
            XCTAssertTrue(0 == self->cacheWrapper[index].virtualizationRoot);
        }
    }
}

@end
