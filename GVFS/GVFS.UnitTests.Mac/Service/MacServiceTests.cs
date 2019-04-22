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
        private const string ExpectedActiveUserId = "502";
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
            this.gvfsPlatformMock.Setup(p => p.GetCurrentUser()).Returns(ExpectedActiveUserId);
            this.gvfsPlatformMock.Setup(p => p.GetUserIdFromLoginSessionId(It.IsAny<int>(), It.IsAny<ITracer>())).Returns<int, ITracer>((x, y) => x.ToString());
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
            Mock<IRepoRegistry> repoRegistry = new Mock<IRepoRegistry>();
            repoRegistry.Setup(r => r.AutoMountRepos(It.IsAny<int>()));

            GVFSService service = new GVFSService(
                this.tracer,
                serviceName: null,
                repoRegistry: repoRegistry.Object,
                gvfsPlatform: this.gvfsPlatformMock.Object);

            service.RunWithArgs(new string[] { $"--servicename={GVFSServiceName}" });

            int expectedUserId = int.Parse(ExpectedActiveUserId);

            repoRegistry.Verify(
                r => r.AutoMountRepos(It.Is<int>(arg => arg == expectedUserId)),
                Times.Once,
                $"{nameof(repoRegistry.Object.AutoMountRepos)} was not called during Service startup");
        }

        [TestCase]
        public void RepoRegistryMountsOnlyRegisteredRepos()
        {
            Mock<IRepoMounter> mountProcessMock = new Mock<IRepoMounter>();
            mountProcessMock.Setup(mp => mp.MountRepository(It.IsAny<string>(), It.IsAny<int>(), It.IsAny<ITracer>())).Returns(true);

            this.CreateTestRepos(this.fileSystem, ServiceDataLocation);

            RepoRegistry repoRegistry = new RepoRegistry(
                this.tracer,
                this.fileSystem,
                ServiceDataLocation,
                this.gvfsPlatformMock.Object,
                mountProcessMock.Object);

            repoRegistry.AutoMountRepos(int.Parse(ExpectedActiveUserId));

            mountProcessMock.Verify(
                mp => mp.MountRepository(
                    It.Is<string>(repo => repo.Equals(ExpectedActiveRepoPath)),
                    It.Is<int>(id => id == int.Parse(ExpectedActiveUserId)),
                    It.IsAny<ITracer>()),
                Times.Once,
                $"{nameof(mountProcessMock.Object.MountRepository)} was not called for registered repository");
        }

        [TestCase]
        public void MountProcessLaunchedUsingCorrectArgs()
        {
            string executable = @"/bin/launchctl";
            string gvfsBinPath = Path.Combine("/", "usr", "local", "vfsforgit", "gvfs");
            string expectedArgs = $"asuser {int.Parse(ExpectedActiveUserId)} {gvfsBinPath} mount {ExpectedActiveRepoPath}";

            Mock<GVFSMountProcess.MountLauncher> mountLauncherMock = new Mock<GVFSMountProcess.MountLauncher>();
            mountLauncherMock.Setup(mp => mp.LaunchProcess(
                It.IsAny<string>(),
                It.IsAny<string>(),
                It.IsAny<string>(),
                It.IsAny<ITracer>()))
                .Returns(true);

            string errorString = null;
            mountLauncherMock.Setup(mp => mp.WaitUntilMounted(
                It.IsAny<string>(),
                It.IsAny<bool>(),
                out errorString))
                .Returns(true);

            GVFSMountProcess mountProcess = new GVFSMountProcess(mountLauncherMock.Object, this.gvfsPlatformMock.Object);
            mountProcess.MountRepository(ExpectedActiveRepoPath, int.Parse(ExpectedActiveUserId), this.tracer);

            mountLauncherMock.Verify(
                mp => mp.LaunchProcess(
                    It.Is<string>(exe => exe.Equals(executable)),
                    It.Is<string>(args => args.Equals(expectedArgs)),
                    It.Is<string>(workingDirectory => workingDirectory.Equals(ExpectedActiveRepoPath)),
                    It.IsAny<ITracer>()),
                Times.Once,
                $"{nameof(mountLauncherMock.Object.LaunchProcess)} was not called for registered repository");
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
