#include "../PrjFSKext/KauthHandlerTestable.hpp"
#include "../PrjFSKext/VirtualizationRoots.hpp"
#include "../PrjFSKext/PrjFSProviderUserClient.hpp"
#include "../PrjFSKext/VirtualizationRootsTestable.hpp"
#include "../PrjFSKext/VnodeCachePrivate.hpp"
#include "../PrjFSKext/VnodeCacheTestable.hpp"
#include "../PrjFSKext/PerformanceTracing.hpp"
#include "../PrjFSKext/public/Message.h"
#include "../PrjFSKext/ProviderMessaging.hpp"
#include "../PrjFSKext/public/PrjFSXattrs.h"
#include "../PrjFSKext/kernel-header-wrappers/kauth.h"
#import <XCTest/XCTest.h>
#import <sys/stat.h>
#include "KextMockUtilities.hpp"
#include "MockVnodeAndMount.hpp"
#include "MockProc.hpp"
#include <tuple>

using std::make_tuple;
using std::shared_ptr;
using std::vector;
using KextMock::_;

class PrjFSProviderUserClient
{
};

static void SetPrjFSFileXattrData(const shared_ptr<vnode>& vnode)
{
    PrjFSFileXAttrData rootXattr = {};
    vector<uint8_t> rootXattrData(sizeof(rootXattr), 0x00);
    memcpy(rootXattrData.data(), &rootXattr, rootXattrData.size());
    vnode->xattrs.insert(make_pair(PrjFSFileXAttrName, rootXattrData));
}

@interface HandleVnodeOperationTests : XCTestCase
@end

@implementation HandleVnodeOperationTests
{
    vfs_context_t context;
    const char* repoPath;
    const char* filePath;
    const char* dirPath;
    PrjFSProviderUserClient dummyClient;
    pid_t dummyClientPid;
    shared_ptr<mount> testMount;
    shared_ptr<vnode> repoRootVnode;
    shared_ptr<vnode> testFileVnode;
    shared_ptr<vnode> testDirVnode;
}

- (void) setUp
{
    kern_return_t initResult = VirtualizationRoots_Init();
    XCTAssertEqual(initResult, KERN_SUCCESS);
    context = vfs_context_create(NULL);
    dummyClientPid = 100;

    // Initialize Vnode Cache
    s_entriesCapacity = 100;
    s_entries = new VnodeCacheEntry[s_entriesCapacity];
    for (uint32_t i = 0; i < s_entriesCapacity; ++i)
    {
        memset(&(s_entries[i]), 0, sizeof(VnodeCacheEntry));
    }

    // Create Vnode Tree
    repoPath = "/Users/test/code/Repo";
    filePath = "/Users/test/code/Repo/file";
    dirPath = "/Users/test/code/Repo/dir";
    testMount = mount::Create();
    repoRootVnode = testMount->CreateVnodeTree(repoPath, VDIR);
    testFileVnode = testMount->CreateVnodeTree(filePath);
    testDirVnode = testMount->CreateVnodeTree(dirPath, VDIR);

    // Register provider for the repository path (Simulate a mount)
    VirtualizationRootResult result = VirtualizationRoot_RegisterProviderForPath(&dummyClient, dummyClientPid, repoPath);
    XCTAssertEqual(result.error, 0);
    vnode_put(s_virtualizationRoots[result.root].rootVNode);
}

- (void) tearDown
{
    testMount.reset();
    repoRootVnode.reset();
    testFileVnode.reset();
    testDirVnode.reset();
    
    s_entriesCapacity = 0;
    delete[] s_entries;
    
    VirtualizationRoots_Cleanup();
    vfs_context_rele(context);
    MockVnodes_CheckAndClear();
    MockCalls::Clear();
}

- (void) testEmptyFileHydrates {
    testFileVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    SetPrjFSFileXattrData(testFileVnode);
    
    const int actionCount = 8;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_READ_ATTRIBUTES,
        KAUTH_VNODE_WRITE_ATTRIBUTES,
        KAUTH_VNODE_READ_EXTATTRIBUTES,
        KAUTH_VNODE_WRITE_EXTATTRIBUTES,
        KAUTH_VNODE_READ_DATA,
        KAUTH_VNODE_WRITE_DATA,
        KAUTH_VNODE_EXECUTE,
        KAUTH_VNODE_DELETE,
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testFileVnode.get()),
            0,
            0);
        XCTAssertTrue(
            MockCalls::DidCallFunction(
                ProviderMessaging_TrySendRequestAndWaitForResponse,
                _,
                MessageType_KtoU_HydrateFile,
                testFileVnode.get(),
                _,
                _,
                _,
                _,
                _,
                nullptr));
        MockCalls::Clear();
    }
}

- (void) testVnodeAccessCausesNoEvent {
    testFileVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    SetPrjFSFileXattrData(testFileVnode);
    
    const int actionCount = 8;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_READ_ATTRIBUTES | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_WRITE_ATTRIBUTES | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_READ_EXTATTRIBUTES | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_WRITE_EXTATTRIBUTES | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_READ_DATA | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_WRITE_DATA | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_EXECUTE | KAUTH_VNODE_ACCESS,
        KAUTH_VNODE_DELETE | KAUTH_VNODE_ACCESS,
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testFileVnode.get()),
            0,
            0);
        XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
        MockCalls::Clear();
    }
}


- (void) testNonEmptyFileDoesNotHydrate {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    
    const int actionCount = 8;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_READ_ATTRIBUTES,
        KAUTH_VNODE_WRITE_ATTRIBUTES,
        KAUTH_VNODE_READ_EXTATTRIBUTES,
        KAUTH_VNODE_WRITE_EXTATTRIBUTES,
        KAUTH_VNODE_READ_DATA,
        KAUTH_VNODE_WRITE_DATA,
        KAUTH_VNODE_EXECUTE,
        KAUTH_VNODE_DELETE,
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testFileVnode.get()),
            0,
            0);
        XCTAssertFalse(
            MockCalls::DidCallFunction(
                ProviderMessaging_TrySendRequestAndWaitForResponse,
                _,
                MessageType_KtoU_HydrateFile,
                testFileVnode.get(),
                _,
                _,
                _,
                _,
                _,
                nullptr));
        MockCalls::Clear();
    }
}

- (void) testNonEmptyFileWithPrjFSFileXAttrNameDoesNotHydrate {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    SetPrjFSFileXattrData(testFileVnode);
    
    const int actionCount = 8;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_READ_ATTRIBUTES,
        KAUTH_VNODE_WRITE_ATTRIBUTES,
        KAUTH_VNODE_READ_EXTATTRIBUTES,
        KAUTH_VNODE_WRITE_EXTATTRIBUTES,
        KAUTH_VNODE_READ_DATA,
        KAUTH_VNODE_WRITE_DATA,
        KAUTH_VNODE_EXECUTE,
        KAUTH_VNODE_DELETE,
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testFileVnode.get()),
            0,
            0);
        XCTAssertFalse(
            MockCalls::DidCallFunction(
                ProviderMessaging_TrySendRequestAndWaitForResponse,
                _,
                MessageType_KtoU_HydrateFile,
                testFileVnode.get(),
                _,
                _,
                _,
                _,
                _,
                nullptr));
        MockCalls::Clear();
    }
}

- (void) testEventsThatShouldNotHydrate {
    testFileVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    
    const int actionCount = 5;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_ADD_SUBDIRECTORY,
        KAUTH_VNODE_DELETE_CHILD,
        KAUTH_VNODE_READ_SECURITY,
        KAUTH_VNODE_WRITE_SECURITY,
        KAUTH_VNODE_TAKE_OWNERSHIP
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testFileVnode.get()),
            0,
            0);
        XCTAssertFalse(
            MockCalls::DidCallFunction(
                ProviderMessaging_TrySendRequestAndWaitForResponse,
                _,
                MessageType_KtoU_HydrateFile,
                testFileVnode.get(),
                _,
                _,
                _,
                _,
                _,
                nullptr));
        MockCalls::Clear();
    }
}

- (void) testDeleteFile {
    testFileVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    HandleVnodeOperation(
        nullptr,
        nullptr,
        KAUTH_VNODE_DELETE,
        reinterpret_cast<uintptr_t>(context),
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        0,
        0);
    XCTAssertTrue(MockCalls::DidCallFunctionsInOrder(
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        make_tuple(
            _,
            MessageType_KtoU_NotifyFilePreDelete,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr),
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        make_tuple(
           _,
            MessageType_KtoU_HydrateFile,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr))
    );
    XCTAssertTrue(MockCalls::CallCount(ProviderMessaging_TrySendRequestAndWaitForResponse) == 2);
}

- (void) testDeleteDir {
    testDirVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    HandleVnodeOperation(
        nullptr,
        nullptr,
        KAUTH_VNODE_DELETE,
        reinterpret_cast<uintptr_t>(context),
        reinterpret_cast<uintptr_t>(testDirVnode.get()),
        0,
        0);
    XCTAssertTrue(MockCalls::DidCallFunctionsInOrder(
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        make_tuple(
            _,
            MessageType_KtoU_NotifyDirectoryPreDelete,
            testDirVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr),
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        make_tuple(
           _,
            MessageType_KtoU_RecursivelyEnumerateDirectory,
            testDirVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr))
    );
    XCTAssertTrue(MockCalls::CallCount(ProviderMessaging_TrySendRequestAndWaitForResponse) == 2);
}

- (void) testEmptyDirectoryEnumerates {
    testDirVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    const int actionCount = 5;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_LIST_DIRECTORY,
        KAUTH_VNODE_SEARCH,
        KAUTH_VNODE_READ_SECURITY,
        KAUTH_VNODE_READ_ATTRIBUTES,
        KAUTH_VNODE_READ_EXTATTRIBUTES
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testDirVnode.get()),
            0,
            0);
        XCTAssertTrue(
            MockCalls::DidCallFunction(
                ProviderMessaging_TrySendRequestAndWaitForResponse,
                _,
                MessageType_KtoU_EnumerateDirectory,
                testDirVnode.get(),
                _,
                _,
                _,
                _,
                _,
                nullptr));
        MockCalls::Clear();
    }
}

- (void) testEventsThatShouldNotDirectoryEnumerates {
    testDirVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    const int actionCount = 9;
    kauth_action_t actions[actionCount] =
    {
       KAUTH_VNODE_WRITE_DATA,
       KAUTH_VNODE_ADD_FILE,
       KAUTH_VNODE_APPEND_DATA,
       KAUTH_VNODE_ADD_SUBDIRECTORY,
       KAUTH_VNODE_DELETE_CHILD,
       KAUTH_VNODE_WRITE_ATTRIBUTES,
       KAUTH_VNODE_WRITE_EXTATTRIBUTES,
       KAUTH_VNODE_WRITE_SECURITY,
       KAUTH_VNODE_TAKE_OWNERSHIP
    };

    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testDirVnode.get()),
            0,
            0);
        XCTAssertFalse(
            MockCalls::DidCallFunction(
                ProviderMessaging_TrySendRequestAndWaitForResponse,
                _,
                MessageType_KtoU_EnumerateDirectory,
                testDirVnode.get(),
                _,
                _,
                _,
                _,
                _,
                nullptr));
        MockCalls::Clear();
    }
}

- (void) testNonEmptyDirectoryDoesNotEnumerate {
    testDirVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    const int actionCount = 5;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_LIST_DIRECTORY,
        KAUTH_VNODE_SEARCH,
        KAUTH_VNODE_READ_SECURITY,
        KAUTH_VNODE_READ_ATTRIBUTES,
        KAUTH_VNODE_READ_EXTATTRIBUTES
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testDirVnode.get()),
            0,
            0);
        XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
    }
}
    
-(void) testWriteFile {
    // If we have FileXattrData attribute we should trigger MessageType_KtoU_NotifyFilePreConvertToFull to remove it
    testFileVnode->attrValues.va_flags = FileFlags_IsEmpty | FileFlags_IsInVirtualizationRoot;
    SetPrjFSFileXattrData(testFileVnode);
    HandleVnodeOperation(
        nullptr,
        nullptr,
        KAUTH_VNODE_WRITE_DATA,
        reinterpret_cast<uintptr_t>(context),
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        0,
        0);
        XCTAssertTrue(MockCalls::DidCallFunctionsInOrder(
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        make_tuple(
            _,
            MessageType_KtoU_HydrateFile,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr),
        ProviderMessaging_TrySendRequestAndWaitForResponse,
        make_tuple(
           _,
            MessageType_KtoU_NotifyFilePreConvertToFull,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr))
    );
    XCTAssertTrue(MockCalls::CallCount(ProviderMessaging_TrySendRequestAndWaitForResponse) == 2);
}

-(void) testWriteFileHydrated {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    // If we have FileXattrData attribute we should trigger MessageType_KtoU_NotifyFilePreConvertToFull to remove it
    SetPrjFSFileXattrData(testFileVnode);
    HandleVnodeOperation(
        nullptr,
        nullptr,
        KAUTH_VNODE_WRITE_DATA,
        reinterpret_cast<uintptr_t>(context),
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        0,
        0);
    XCTAssertFalse(
       MockCalls::DidCallFunction(
            ProviderMessaging_TrySendRequestAndWaitForResponse,
            _,
            MessageType_KtoU_HydrateFile,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr));
    XCTAssertTrue(
       MockCalls::DidCallFunction(
            ProviderMessaging_TrySendRequestAndWaitForResponse,
            _,
            MessageType_KtoU_NotifyFilePreConvertToFull,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            nullptr));
    XCTAssertTrue(MockCalls::CallCount(ProviderMessaging_TrySendRequestAndWaitForResponse) == 1);
}

-(void) testWriteFileFull {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;
    HandleVnodeOperation(
        nullptr,
        nullptr,
        KAUTH_VNODE_WRITE_DATA,
        reinterpret_cast<uintptr_t>(context),
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        0,
        0);
    XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
}

- (void) testEventsAreIgnored {
    testDirVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot | FileFlags_IsEmpty;
    const int actionCount = 5;
    kauth_action_t actions[actionCount] =
    {
        KAUTH_VNODE_APPEND_DATA,
        KAUTH_VNODE_ADD_SUBDIRECTORY,
        KAUTH_VNODE_WRITE_SECURITY,
        KAUTH_VNODE_TAKE_OWNERSHIP,
        KAUTH_VNODE_ACCESS
    };
    
    for (int i = 0; i < actionCount; i++)
    {
        // Verify for File node
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testFileVnode.get()),
            0,
            0);
        XCTAssertFalse(
            MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
        MockCalls::Clear();

        // Verify for Directory node
        HandleVnodeOperation(
            nullptr,
            nullptr,
            actions[i],
            reinterpret_cast<uintptr_t>(context),
            reinterpret_cast<uintptr_t>(testDirVnode.get()),
            0,
            0);
        XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
        MockCalls::Clear();
    }
}

- (void) testOpen {
    // KAUTH_FILEOP_OPEN should trigger callbacks for files not flagged as in the virtualization root.
    testFileVnode->attrValues.va_flags = 0;
    HandleFileOpOperation(
        nullptr,
        nullptr,
        KAUTH_FILEOP_OPEN,
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        reinterpret_cast<uintptr_t>(filePath),
        0,
        0);
    
    XCTAssertTrue(
       MockCalls::DidCallFunction(
            ProviderMessaging_TrySendRequestAndWaitForResponse,
            _,
            MessageType_KtoU_NotifyFileCreated,
            testFileVnode.get(),
            _,
            _,
            _,
            _,
            _,
            _));
}

- (void) testOpenInVirtualizationRoot {
    // KAUTH_FILEOP_OPEN should not trigger any callbacks for files that already flagged as in the virtualization root.
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;

    HandleFileOpOperation(
        nullptr,
        nullptr,
        KAUTH_FILEOP_OPEN,
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        reinterpret_cast<uintptr_t>(filePath),
        0,
        0);
    
    XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
}

- (void) testCloseWithModifed {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;

    HandleFileOpOperation(
        nullptr,
        nullptr,
        KAUTH_FILEOP_CLOSE,
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        reinterpret_cast<uintptr_t>(filePath),
        KAUTH_FILEOP_CLOSE_MODIFIED,
        0);
    
    XCTAssertTrue(
       MockCalls::DidCallFunction(
            ProviderMessaging_TrySendRequestAndWaitForResponse,
            _,
            MessageType_KtoU_NotifyFileModified,
            testFileVnode.get(),
            _,
            filePath,
            _,
            _,
            _,
            _));
}

- (void) testCloseWithModifedOnDirectory {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;

    HandleFileOpOperation(
        nullptr,
        nullptr,
        KAUTH_FILEOP_CLOSE,
        reinterpret_cast<uintptr_t>(testDirVnode.get()),
        reinterpret_cast<uintptr_t>(dirPath),
        KAUTH_FILEOP_CLOSE_MODIFIED,
        0);
    
    XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
}


- (void) testCloseWithoutModifed {
    testFileVnode->attrValues.va_flags = FileFlags_IsInVirtualizationRoot;

    HandleFileOpOperation(
        nullptr,
        nullptr,
        KAUTH_FILEOP_CLOSE,
        reinterpret_cast<uintptr_t>(testFileVnode.get()),
        reinterpret_cast<uintptr_t>(filePath),
        0,
        0);
    
    XCTAssertFalse(MockCalls::DidCallFunction(ProviderMessaging_TrySendRequestAndWaitForResponse));
}


@end
