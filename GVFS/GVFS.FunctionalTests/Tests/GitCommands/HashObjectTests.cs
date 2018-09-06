using GVFS.FunctionalTests.Should;
using GVFS.FunctionalTests.Tools;
using GVFS.Tests.Should;
using NUnit.Framework;
using System.IO;

namespace GVFS.FunctionalTests.Tests.GitCommands
{
    [TestFixture]
    [Category(Categories.GitCommands)]
    public class HashObjectTests : GitRepoTests
    {
        public HashObjectTests() : base(enlistmentPerTest: false, validateRepoInSetup: false)
        {
        }

        [TestCase]
        public void CanReadFileAfterHashObject()
        {
            this.ValidateGitCommand("status");

            // Validate that RunUnitTests.bat is not on disk at all
            string fileName = Path.Combine("Scripts", "RunUnitTests.bat");

            this.Enlistment.UnmountGVFS();
            this.Enlistment.GetVirtualPathTo(fileName).ShouldNotExistOnDisk(this.FileSystem);
            this.Enlistment.MountGVFS();

            GitHelpers.InvokeGitAgainstGVFSRepo(
                this.Enlistment.RepoRoot,
                "hash-object " + fileName).ExitCode.ShouldEqual(0);

            while (!File.Exists(this.Enlistment.GetVirtualPathTo(fileName)))
            {
                System.Threading.Thread.Sleep(500);
            }

            this.FileContentsShouldMatch(fileName);
        }
    }
}
