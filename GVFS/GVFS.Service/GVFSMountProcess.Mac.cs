using GVFS.Common;
using GVFS.Common.Tracing;
using System.Diagnostics;

namespace GVFS.Service
{
    public class GVFSMountProcess : IRepoMountProcess
    {
        private const string ExecutablePath = "/bin/launchctl";
        private const string ArgumentFormat = "asuser {0} /usr/local/vfsforgit/gvfs mount {1}";

        private MountLauncher processLauncher;
        private bool waitTillMounted;

        public GVFSMountProcess() : this(new MountLauncher(), waitTillMounted: true)
        {
        }

        public GVFSMountProcess(MountLauncher processLauncher, bool waitTillMounted)
        {
            this.processLauncher = processLauncher;
            this.waitTillMounted = waitTillMounted;
        }

        public virtual bool MountRepository(string repoRoot, int sessionId, ITracer tracer)
        {
            string arguments = string.Format(ArgumentFormat, sessionId, repoRoot);

            if (!this.processLauncher.LaunchProcess(ExecutablePath, arguments, repoRoot, tracer))
            {
                tracer.RelatedError($"{nameof(this.MountRepository)}: Unable to start the GVFS process.");
                return false;
            }

            string errorMessage;
            if (this.waitTillMounted && !this.WaitUntilMounted(repoRoot, false, out errorMessage))
            {
                tracer.RelatedError(errorMessage);
                return false;
            }

            return true;
        }

        public virtual bool WaitUntilMounted(string repoRoot, bool unattended, out string errorMessage)
        {
            return GVFSEnlistment.WaitUntilMounted(repoRoot, unattended, out errorMessage);
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
