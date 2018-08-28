﻿using NUnit.Framework;

namespace GVFS.FunctionalTests.Tests.GitCommands
{
    [TestFixture]
    [Category(Categories.GitCommands)]
    [Category(Categories.Mac.M4)]
    public class UpdateRefTests : GitRepoTests
    {
        public UpdateRefTests() : base(enlistmentPerTest: true)
        {
        }

        [TestCase]
        public void UpdateRefModifiesHead()
        {
            this.ValidateGitCommand("status");
            this.ValidateGitCommand("update-ref HEAD f1bce402a7a980a8320f3f235cf8c8fdade4b17a");
        }

        [TestCase]
        public void UpdateRefModifiesHeadThenResets()
        {
            this.ValidateGitCommand("status");
            this.ValidateGitCommand("update-ref HEAD f1bce402a7a980a8320f3f235cf8c8fdade4b17a");
            this.ValidateGitCommand("reset HEAD");
        }

        public override void TearDownForTest()
        {
            // Need to ignore case changes in this test because the update-ref will have
            // folder names that only changed the case and when checking the folder structure
            // it will create partial folders with that case and will not get updated to the
            // previous case when the reset --hard running in the tear down step
            this.TestValidationAndCleanup(ignoreCase: true);
        }
    }
}
