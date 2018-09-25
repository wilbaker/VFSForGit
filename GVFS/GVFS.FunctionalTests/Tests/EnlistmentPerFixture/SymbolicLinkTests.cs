using GVFS.FunctionalTests.FileSystemRunners;
using NUnit.Framework;

namespace GVFS.FunctionalTests.Tests.EnlistmentPerFixture
{
    // MacOnly until issue #297 (add SymLink support for Windows) is complete
    [Category(Categories.MacOnly)]
    [TestFixtureSource(typeof(FileSystemRunner), FileSystemRunner.TestRunners)]
    public class SymbolicLinkTests : TestsWithEnlistmentPerFixture
    {
        private FileSystemRunner fileSystem;
        public SymbolicLinkTests(FileSystemRunner fileSystem)
        {
            this.fileSystem = fileSystem;
        }

        [TestCase, Order(1)]
        public void CheckoutBranchWithSymLinks()
        {
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
