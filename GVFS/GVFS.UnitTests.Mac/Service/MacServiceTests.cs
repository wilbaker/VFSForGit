using GVFS.Common.FileSystem;
using GVFS.Common.Tracing;
using GVFS.Platform.Mac;
using GVFS.Service;
using GVFS.UnitTests.Mock.Common;
using GVFS.UnitTests.Mock.FileSystem;
using Moq;
using NUnit.Framework;
using System.IO;

namespace GVFS.UnitTests.Mac.Service
{
    [TestFixture]
    [NonParallelizable]
    public class MacServiceTests
    {
        private const string GVFSServiceName = "GVFS.Service";
        private const int ExpectedActiveUserId = 502;
        private static readonly string ExpectedActiveRepoPath = Path.Combine("mock:", "code", "repo2");
        private static readonly string ServiceDataLocation = Path.Combine("mock:", "registryDataFolder");

        private MockFileSystem fileSystem;
        private MockTracer tracer;
        private Mock<MacPlatform> gvfsPlatformMock;

        [SetUp]
        public void SetUp()
        {
            this.tracer = new MockTracer();
            this.fileSystem = new MockFileSystem(new MockDirectory(ServiceDataLocation, null, null));
            this.gvfsPlatformMock = new Mock<MacPlatform>();
            this.gvfsPlatformMock.Setup(p => p.GetCurrentUser()).Returns(ExpectedActiveUserId.ToString());
            this.gvfsPlatformMock.Setup(p => p.GetUserIdFromLoginSessionId(ExpectedActiveUserId, It.IsAny<ITracer>())).Returns<int, ITracer>((x, y) => x.ToString());
            this.gvfsPlatformMock.SetupGet(p => p.FileSystem).Returns(new MockPlatformFileSystem());
            this.gvfsPlatformMock.SetupGet(p => p.Constants).Returns(new MacPlatform.MacPlatformConstants());
        }

        [TearDown]
        public void TearDown()
        {
            this.gvfsPlatformMock = null;
            this.fileSystem = null;
            this.tracer = null;
        }

        [TestCase]
        public void ServiceStartTriggersAutoMountForCurrentUser()
        {
            Mock<IRepoRegistry> repoRegistry = new Mock<IRepoRegistry>(MockBehavior.Strict);
            repoRegistry.Setup(r => r.AutoMountRepos(ExpectedActiveUserId));
            repoRegistry.Setup(r => r.TraceStatus());

            GVFSService service = new GVFSService(
                this.tracer,
                serviceName: null,
                repoRegistry: repoRegistry.Object,
                gvfsPlatform: this.gvfsPlatformMock.Object);

            service.RunWithArgs(new string[] { $"--servicename={GVFSServiceName}" });

            repoRegistry.VerifyAll();
        }

        [TestCase]
        public void RepoRegistryMountsOnlyRegisteredRepos()
        {
            Mock<IRepoMounter> mountProcessMock = new Mock<IRepoMounter>(MockBehavior.Strict);
            mountProcessMock.Setup(mp => mp.MountRepository(ExpectedActiveRepoPath, ExpectedActiveUserId)).Returns(true);

            this.CreateTestRepos(this.fileSystem, ServiceDataLocation);

            RepoRegistry repoRegistry = new RepoRegistry(
                this.tracer,
                this.fileSystem,
                ServiceDataLocation,
                this.gvfsPlatformMock.Object,
                mountProcessMock.Object);

            repoRegistry.AutoMountRepos(ExpectedActiveUserId);

            mountProcessMock.VerifyAll();
        }

        [TestCase]
        public void MountProcessLaunchedUsingCorrectArgs()
        {
            string executable = @"/bin/launchctl";
            string gvfsBinPath = Path.Combine("/", "usr", "local", "vfsforgit", "gvfs");
            string expectedArgs = $"asuser {ExpectedActiveUserId} {gvfsBinPath} mount {ExpectedActiveRepoPath}";

            Mock<GVFSMountProcess.MountLauncher> mountLauncherMock = new Mock<GVFSMountProcess.MountLauncher>(MockBehavior.Strict, this.tracer);
            mountLauncherMock.Setup(mp => mp.LaunchProcess(
                executable,
                expectedArgs,
                ExpectedActiveRepoPath))
                .Returns(true);

            string errorString = null;
            mountLauncherMock.Setup(mp => mp.WaitUntilMounted(
                ExpectedActiveRepoPath,
                It.IsAny<bool>(),
                out errorString))
                .Returns(true);

            GVFSMountProcess mountProcess = new GVFSMountProcess(this.tracer, mountLauncherMock.Object, this.gvfsPlatformMock.Object);
            mountProcess.MountRepository(ExpectedActiveRepoPath, ExpectedActiveUserId);

            mountLauncherMock.VerifyAll();
        }

        private void CreateTestRepos(PhysicalFileSystem fileSystem, string dataLocation)
        {
            string repo1 = Path.Combine("mock:", "code", "repo1");
            string repo2 = ExpectedActiveRepoPath;
            string repo3 = Path.Combine("mock:", "code", "repo3");
            string repo4 = Path.Combine("mock:", "code", "repo4");

            this.fileSystem.WriteAllText(
                Path.Combine(dataLocation, RepoRegistry.RegistryName),
                $@"1
                {{""EnlistmentRoot"":""{repo1.Replace("\\", "\\\\")}"",""OwnerSID"":502,""IsActive"":false}}
                {{""EnlistmentRoot"":""{repo2.Replace("\\", "\\\\")}"",""OwnerSID"":502,""IsActive"":true}}
                {{""EnlistmentRoot"":""{repo3.Replace("\\", "\\\\")}"",""OwnerSID"":501,""IsActive"":false}}
                {{""EnlistmentRoot"":""{repo4.Replace("\\", "\\\\")}"",""OwnerSID"":501,""IsActive"":true}}
                ");
        }
    }
}
