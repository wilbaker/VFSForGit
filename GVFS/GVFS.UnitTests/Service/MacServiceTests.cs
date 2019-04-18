using GVFS.Common;
using GVFS.Common.FileSystem;
using GVFS.Common.Tracing;
using GVFS.Service;
using GVFS.UnitTests.Mock.Common;
using GVFS.UnitTests.Mock.FileSystem;
using Moq;
using NUnit.Framework;
using System.IO;

namespace GVFS.UnitTests.Service
{
    [TestFixture]
    [NonParallelizable]
    public class MacServiceTests
    {
        private const string GVFSServiceName = "GVFS.Service";
        private const string ActiveUserId = "502";
        private static readonly string RegisteredRepo = Path.Combine("mock:", "code", "repo2");
        private static readonly string ServiceDataLocation = Path.Combine("mock:", "registryDataFolder");

        private MockFileSystem fileSystem;
        private MockTracer tracer;
        private Mock<MockPlatform> gvfsPlatformMock;

        [SetUp]
        public void SetUp()
        {
            this.tracer = new MockTracer();
            this.fileSystem = new MockFileSystem(new MockDirectory(ServiceDataLocation, null, null));
            this.gvfsPlatformMock = new Mock<MockPlatform>();
            this.gvfsPlatformMock.Setup(p => p.GetCurrentUser()).Returns(ActiveUserId);
        }

        [TearDown]
        public void TearDown()
        {
            this.fileSystem = null;
            this.tracer = null;
        }

        [TestCase]
        public void ServiceStartTriggersAutoMountForCurrentUser()
        {
            Mock<RepoRegistry> repoRegistry = new Mock<RepoRegistry>();
            repoRegistry.Setup(r => r.AutoMountRepos(It.IsAny<int>()));

            GVFSService service = new GVFSService(
                this.tracer,
                GVFSServiceName,
                startListening: false,
                repoRegistry: repoRegistry.Object,
                gvfsPlatform: this.gvfsPlatformMock.Object);

            service.RunWithArgs(new string[] { $"--servicename={GVFSServiceName}" });

            int expectedUserId = int.Parse(ActiveUserId);

            repoRegistry.Verify(
                r => r.AutoMountRepos(It.Is<int>(arg => arg == expectedUserId)),
                Times.Once,
                $"{nameof(repoRegistry.Object.AutoMountRepos)} was not called during Service startup");

            repoRegistry.Verify(
                r => r.TraceStatus(),
                Times.Once,
                $"{nameof(repoRegistry.Object.TraceStatus)} was not called during Service startup");
        }

        [TestCase]
        public void RepoRegistryMountsOnlyRegisteredRepos()
        {
            Mock<GVFSMountProcess> mountProcessMock = new Mock<GVFSMountProcess>();
            mountProcessMock.Setup(mp => mp.MountRepository(It.IsAny<string>(), It.IsAny<int>(), It.IsAny<ITracer>()));

            this.CreateTestRepos(this.fileSystem, ServiceDataLocation);

            MockPlatform gvfsPlatform = new MockPlatform();
            RepoRegistry repoRegistry = new RepoRegistry(
                this.tracer,
                this.fileSystem,
                ServiceDataLocation,
                gvfsPlatform,
                mountProcessMock.Object);

            repoRegistry.AutoMountRepos(int.Parse(ActiveUserId));

            mountProcessMock.Verify(
                mp => mp.MountRepository(
                    It.Is<string>(repo => repo.Equals(RegisteredRepo)),
                    It.Is<int>(id => id == int.Parse(ActiveUserId)),
                    It.IsAny<ITracer>()),
                Times.Once,
                $"{nameof(mountProcessMock.Object.MountRepository)} was not called for registered repository");
        }

        [TestCase]
        public void MountProcessLaunchedUsingCorrectArgs()
        {
            string executable = @"/bin/launchctl";
            string expectedArgs = $"asuser {int.Parse(ActiveUserId)} /usr/local/vfsforgit/gvfs mount {RegisteredRepo}";

            Mock<GVFSMountProcess.MountLauncher> mountLauncherMock = new Mock<GVFSMountProcess.MountLauncher>();
            mountLauncherMock.Setup(mp => mp.LaunchProcess(
                It.IsAny<string>(),
                It.IsAny<string>(),
                It.IsAny<string>(),
                It.IsAny<ITracer>()))
                .Returns(true);

            GVFSMountProcess mountProcess = new GVFSMountProcess(mountLauncherMock.Object, waitTillMounted: false);
            mountProcess.MountRepository(RegisteredRepo, int.Parse(ActiveUserId), this.tracer);

            mountLauncherMock.Verify(
                mp => mp.LaunchProcess(
                    It.Is<string>(exe => exe.Equals(executable)),
                    It.Is<string>(args => args.Equals(expectedArgs)),
                    It.Is<string>(workingDirectory => workingDirectory.Equals(RegisteredRepo)),
                    It.IsAny<ITracer>()),
                Times.Once,
                $"{nameof(mountLauncherMock.Object.LaunchProcess)} was not called for registered repository");
        }

        private void CreateTestRepos(PhysicalFileSystem fileSystem, string dataLocation)
        {
            string repo1 = Path.Combine("mock:", "code", "repo1");
            string repo2 = Path.Combine("mock:", "code", "repo2");
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
