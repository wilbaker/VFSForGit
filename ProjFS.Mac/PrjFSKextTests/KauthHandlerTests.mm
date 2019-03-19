#include "../PrjFSKext/kernel-header-wrappers/vnode.h"
#include "../PrjFSKext/KauthHandlerTestable.hpp"
#include "../PrjFSKext/VirtualizationRoots.hpp"
#include "../PrjFSKext/PrjFSProviderUserClient.hpp"
#include "../PrjFSKext/VirtualizationRootsTestable.hpp"
#include "../PrjFSKext/PerformanceTracing.hpp"
#include "../PrjFSKext/public/Message.h"
#include "../PrjFSKext/ProviderMessaging.hpp"
#include "../PrjFSKext/public/PrjFSXattrs.h"
#import <XCTest/XCTest.h>
#include "KextMockUtilities.hpp"
#include "MockVnodeAndMount.hpp"
#include "MockProc.hpp"

using std::shared_ptr;

class PrjFSProviderUserClient
{
};

bool ProviderMessaging_TrySendRequestAndWaitForResponse(
    VirtualizationRootHandle root,
    MessageType messageType,
    const vnode_t vnode,
    const FsidInode& vnodeFsidInode,
    const char* vnodePath,
    int pid,
    const char* procname,
    int* kauthResult,
    int* kauthError)
{
    MockCalls::RecordFunctionCall(
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        root,
        messageType,
        vnode,
        vnodeFsidInode,
        vnodePath,
        pid,
        procname,
        kauthResult,
        kauthError);
      
    return true;
}

@interface KauthHandlerTests : XCTestCase
@end

@implementation KauthHandlerTests

- (void) tearDown
{
    MockVnodes_CheckAndClear();
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
    shared_ptr<mount> testMount = mount::Create("hfs", fsid_t{}, 0);
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
    
    testVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), NULL, &fileFlaggedInRoot));
    XCTAssertTrue(fileFlaggedInRoot);
    
    testVnode->attrValues.va_flags = FileFlags_IsEmpty;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), NULL, &fileFlaggedInRoot));
    XCTAssertFalse(fileFlaggedInRoot);
    
    testVnode->attrValues.va_flags = FileFlags_Invalid;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), NULL, &fileFlaggedInRoot));
    XCTAssertFalse(fileFlaggedInRoot);
    
    testVnode->attrValues.va_flags = 0x00000100;
    XCTAssertTrue(TryGetFileIsFlaggedAsInRoot(testVnode.get(), NULL, &fileFlaggedInRoot));
    XCTAssertFalse(fileFlaggedInRoot);

    testVnode->errors.getattr = EBADF;
    XCTAssertFalse(TryGetFileIsFlaggedAsInRoot(testVnode.get(), NULL, &fileFlaggedInRoot));
}

- (void)testShouldHandleVnodeOpEvent {
    // In Parameters
    shared_ptr<mount> testMount = mount::Create();
    shared_ptr<vnode> testVnode = vnode::Create(testMount, "/foo");
    testVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    PerfTracer perfTracer;
    vfs_context_t _Nonnull context = vfs_context_create(NULL);
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
    
    
    // Test Invalid Action Bit
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

/*
KEXT_STATIC int HandleVnodeOperation(
    kauth_cred_t    credential,
    void*           idata,
    kauth_action_t  action,
    uintptr_t       arg0,
    uintptr_t       arg1,
    uintptr_t       arg2,
    uintptr_t       arg3);
*/
- (void)testHandleVnodeOpEvent {
    // Setup
    kern_return_t initResult = VirtualizationRoots_Init();
    XCTAssertEqual(initResult, KERN_SUCCESS);

    // Parameters
    const char* repoPath = "/Users/test/code/Repo";
    const char* filePath = "/Users/test/code/Repo/file";
    vfs_context_t _Nonnull context = vfs_context_create(NULL);
    PerfTracer perfTracer;
    PrjFSProviderUserClient dummyClient;
    pid_t dummyClientPid=100;

    // Create Vnode Tree
    shared_ptr<mount> testMount = mount::Create();
    shared_ptr<vnode> repoRootVnode = testMount->CreateVnodeTree(repoPath, VDIR);
    shared_ptr<vnode> testFileVnode = testMount->CreateVnodeTree(filePath);

    // Register provider for the repository path (Simulate a mount)
    VirtualizationRootResult result = VirtualizationRoot_RegisterProviderForPath(&dummyClient, dummyClientPid, repoPath);
    XCTAssertEqual(result.error, 0);
    vnode_put(s_virtualizationRoots[result.root].rootVNode);
    
    // Read a file that has not been hydrated yet
    testFileVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    HandleVnodeOperation(
        nullptr,
        nullptr,
        KAUTH_VNODE_READ_DATA,
        reinterpret_cast<uintptr_t>(context),
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        0,
        0);

    // Teardown
    VirtualizationRoots_Cleanup();
}


@end
