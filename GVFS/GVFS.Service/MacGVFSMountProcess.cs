using GVFS.Common;
using GVFS.Common.Tracing;
using System.Diagnostics;
using System.IO;

namespace GVFS.Service
{
    public class MacGVFSMountProcess : IRepoMounter
    {
        private const string ExecutablePath = "/bin/launchctl";

        private MountLauncher processLauncher;
        private GVFSPlatform platform;
        private ITracer tracer;

        public MacGVFSMountProcess(ITracer tracer, MountLauncher processLauncher = null, GVFSPlatform platform = null)
        {
            this.tracer = tracer;
            this.processLauncher = processLauncher ?? new MountLauncher(tracer);
            this.platform = platform ?? GVFSPlatform.Instance;
        }

        public bool MountRepository(string repoRoot, int sessionId)
        {
            string arguments = string.Format(
                "asuser {0} {1} mount {2}",
                sessionId,
                Path.Combine(this.platform.Constants.GVFSBinDirectoryPath, this.platform.Constants.GVFSExecutableName),
                repoRoot);

            if (!this.processLauncher.LaunchProcess(ExecutablePath, arguments, repoRoot))
            {
                this.tracer.RelatedError($"{nameof(this.MountRepository)}: Unable to start the GVFS process.");
                return false;
            }

            string errorMessage;
            if (!this.processLauncher.WaitUntilMounted(repoRoot, false, out errorMessage))
            {
                this.tracer.RelatedError(errorMessage);
                return false;
            }

            return true;
        }

        public class MountLauncher
        {
            private ITracer tracer;

            public MountLauncher(ITracer tracer)
            {
                this.tracer = tracer;
            }

            public virtual bool LaunchProcess(string executablePath, string arguments, string workingDirectory)
            {
                ProcessStartInfo processInfo = new ProcessStartInfo(executablePath);
                processInfo.Arguments = arguments;
                processInfo.WindowStyle = ProcessWindowStyle.Hidden;
                processInfo.WorkingDirectory = workingDirectory;
                processInfo.UseShellExecute = false;
                processInfo.RedirectStandardOutput = true;

                ProcessResult result = ProcessHelper.Run(processInfo);
                if (result.ExitCode != 0)
                {
                    this.tracer.RelatedError($"{nameof(this.LaunchProcess)} ERROR: {executablePath} {arguments}. Exit code: {result.ExitCode}, {result.Errors}");
                    return false;
                }

                return true;
            }

            public virtual bool WaitUntilMounted(string enlistmentRoot, bool unattended, out string errorMessage)
            {
                return GVFSEnlistment.WaitUntilMounted(enlistmentRoot, false, out errorMessage);
            }
        }
    }
}
