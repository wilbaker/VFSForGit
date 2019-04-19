using GVFS.Common;
using GVFS.Common.Tracing;
using System.Diagnostics;

namespace GVFS.Service
{
    public class GVFSMountProcess : IRepoMounter
    {
        private const string ExecutablePath = "/bin/launchctl";
        private const string GVFSPath = "/usr/local/vfsforgit/gvfs";

        private MountLauncher processLauncher;
        private bool waitTillMounted;

        public GVFSMountProcess(MountLauncher processLauncher = null, bool waitTillMounted = false)
        {
            this.processLauncher = processLauncher ?? new MountLauncher();
            this.waitTillMounted = waitTillMounted;
        }

        public bool MountRepository(string repoRoot, int sessionId, ITracer tracer)
        {
            string arguments = string.Format("asuser {0} {1} mount {2}", sessionId, GVFSPath, repoRoot);

            if (!this.processLauncher.LaunchProcess(ExecutablePath, arguments, repoRoot, tracer))
            {
                tracer.RelatedError($"{nameof(this.MountRepository)}: Unable to start the GVFS process.");
                return false;
            }

            string errorMessage;
            if (this.waitTillMounted && !GVFSEnlistment.WaitUntilMounted(repoRoot, false, out errorMessage))
            {
                tracer.RelatedError(errorMessage);
                return false;
            }

            return true;
        }

        public string GetUserId(int sessionId)
        {
            return sessionId.ToString();
        }

        public class MountLauncher
        {
            public virtual bool LaunchProcess(string executablePath, string arguments, string workingDirectory, ITracer tracer)
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
                    tracer.RelatedError($"{nameof(this.LaunchProcess)} ERROR: {executablePath} {arguments}. Exit code: {result.ExitCode}, {result.Errors}");
                    return false;
                }

                return true;
            }
        }
    }
}
