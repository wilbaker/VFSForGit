﻿using NUnit.Framework;

namespace GVFS.FunctionalTests.Tests.GitCommands
{
    [TestFixture]
    [Category(Categories.GitCommands)]
    [Category(Categories.Mac.M3TODO)]
    public class ResetSoftTests : GitRepoTests
    {
        public ResetSoftTests() : base(enlistmentPerTest: true)
        {
        }

        [TestCase]
        public void ResetSoft()
        {
            this.ValidateGitCommand("checkout " + GitRepoTests.ConflictTargetBranch);
            this.ValidateGitCommand("reset --soft HEAD~1");
            this.FilesShouldMatchCheckoutOfTargetBranch();
        }

        [TestCase]
        public void ResetSoftThenRemount()
        {
            this.ValidateGitCommand("checkout " + GitRepoTests.ConflictTargetBranch);
            this.ValidateGitCommand("reset --soft HEAD~1");
            this.FilesShouldMatchCheckoutOfTargetBranch();

            this.Enlistment.UnmountGVFS();
            this.Enlistment.MountGVFS();
            this.ValidateGitCommand("status");
            this.FilesShouldMatchCheckoutOfTargetBranch();
        }

        [TestCase]
        public void ResetSoftThenCheckoutWithConflicts()
        {
            this.ValidateGitCommand("checkout " + GitRepoTests.ConflictTargetBranch);
            this.ValidateGitCommand("reset --soft HEAD~1");
            this.ValidateGitCommand("checkout " + GitRepoTests.ConflictSourceBranch);
            this.FilesShouldMatchCheckoutOfTargetBranch();
        }

        [TestCase]
        public void ResetSoftThenCheckoutNoConflicts()
        {
            this.ValidateGitCommand("checkout " + GitRepoTests.ConflictTargetBranch);
            this.ValidateGitCommand("reset --soft HEAD~1");
            this.ValidateGitCommand("checkout " + GitRepoTests.NoConflictSourceBranch);
            this.FilesShouldMatchAfterNoConflict();
        }

        [TestCase]
        public void ResetSoftThenResetHeadThenCheckoutNoConflicts()
        {
            this.ValidateGitCommand("checkout " + GitRepoTests.ConflictTargetBranch);
            this.ValidateGitCommand("reset --soft HEAD~1");
            this.ValidateGitCommand("reset HEAD Test_ConflictTests/AddedFiles/AddedByBothDifferentContent.txt");
            this.ValidateGitCommand("checkout " + GitRepoTests.NoConflictSourceBranch);
            this.FilesShouldMatchAfterNoConflict();
        }

        protected override void CreateEnlistment()
        {
            base.CreateEnlistment();
            this.ControlGitRepo.Fetch(GitRepoTests.ConflictTargetBranch);
            this.ControlGitRepo.Fetch(GitRepoTests.ConflictSourceBranch);
            this.ControlGitRepo.Fetch(GitRepoTests.NoConflictSourceBranch);
        }
    }
}
