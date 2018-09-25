using System.IO;
using GVFS.FunctionalTests.FileSystemRunners;
using GVFS.FunctionalTests.Should;
using GVFS.FunctionalTests.Tools;
using GVFS.Tests.Should;
using NUnit.Framework;

namespace GVFS.FunctionalTests.Tests.EnlistmentPerFixture
{
    // MacOnly until issue #297 (add SymLink support for Windows) is complete
    [Category(Categories.MacOnly)]
    [TestFixtureSource(typeof(FileSystemRunner), FileSystemRunner.TestRunners)]
    public class SymbolicLinkTests : TestsWithEnlistmentPerFixture
    {
        private const string TestFolderName = "Test_EPF_SymbolicLinks";

        private const string TestFileName = "TestFile.txt";
        private const string TestFileContents = "This is a real file";
        private const string TestFile2Name = "TestFile2.txt";
        private const string TestFile2Contents = "This is the second real file";
        private const string ChildFolderName = "ChildDir";
        private const string ChildLinkName = "LinkToFileInFolder";
        private const string GrandChildLinkName = "LinkToFileInParentFolder";

        private BashRunner bashRunner;
        public SymbolicLinkTests()
        {
            this.bashRunner = new BashRunner();
        }

        [TestCase, Order(1)]
        public void CheckoutBranchWithSymLinks()
        {
            GitHelpers.CheckGitCommandAgainstGVFSRepo(
                this.Enlistment.RepoRoot, 
                "checkout FunctionalTests/20180925_SymLinksPart1",
                "Switched to branch 'FunctionalTests/20180925_SymLinksPart1'");

            string testFilePath = this.Enlistment.GetVirtualPathTo(Path.Combine(TestFolderName, TestFileName));
            testFilePath.ShouldBeAFile(this.bashRunner).WithContents(TestFileContents);
            this.bashRunner.IsSymbolicLink(testFilePath).ShouldBeFalse($"{testFilePath} should not be a symlink");

            string testFile2Path = this.Enlistment.GetVirtualPathTo(Path.Combine(TestFolderName, TestFile2Name));
            testFile2Path.ShouldBeAFile(this.bashRunner).WithContents(TestFile2Contents);
            this.bashRunner.IsSymbolicLink(testFile2Path).ShouldBeFalse($"{testFile2Path} should not be a symlink");

            string childLinkPath = this.Enlistment.GetVirtualPathTo(Path.Combine(TestFolderName, ChildLinkName));
            this.bashRunner.IsSymbolicLink(childLinkPath).ShouldBeTrue($"{childLinkPath} should be a symlink");
            childLinkPath.ShouldBeAFile(this.bashRunner).WithContents(TestFileContents);

            string grandChildLinkPath = this.Enlistment.GetVirtualPathTo(Path.Combine(TestFolderName, ChildFolderName, GrandChildLinkName));
            this.bashRunner.IsSymbolicLink(grandChildLinkPath).ShouldBeTrue($"{grandChildLinkPath} should be a symlink");
            grandChildLinkPath.ShouldBeAFile(this.bashRunner).WithContents(TestFile2Contents);
        }

        [TestCase, Order(2)]
        public void CheckoutBranchWhereSymLinksTransitionToFiles()
        {
        }

        [TestCase, Order(3)]
        public void CheckoutBranchWhereFilesTransitionToSymLinks()
        {
        }

        [TestCase, Order(4)]
        public void GitStatusReportsSymLinkChanges()
        {
        }
    }
}
