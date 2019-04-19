using GVFS.Common;
using GVFS.Common.Tracing;
using GVFS.Platform.Windows;
using GVFS.Service.Handlers;

namespace GVFS.Service
{
    public class GVFSMountProcess : IRepoMounter
    {
        public bool MountRepository(string repoRoot, int sessionId, ITracer tracer)
        {
            if (!ProjFSFilter.IsServiceRunning(tracer))
            {
                string error;
                if (!EnableAndAttachProjFSHandler.TryEnablePrjFlt(tracer, out error))
                {
                    tracer.RelatedError($"{nameof(this.MountRepository)}: Could not enable PrjFlt: {error}");
                    return false;
                }
            }

            using (CurrentUser currentUser = new CurrentUser(tracer, sessionId))
            {
                if (!this.CallGVFSMount(repoRoot, currentUser))
                {
                    tracer.RelatedError($"{nameof(this.MountRepository)}: Unable to start the GVFS.exe process.");
                    return false;
                }

                string errorMessage;
                if (!GVFSEnlistment.WaitUntilMounted(repoRoot, false, out errorMessage))
                {
                    tracer.RelatedError(errorMessage);
                    return false;
                }
            }

            return true;
        }

        private bool CallGVFSMount(string repoRoot, CurrentUser currentUser)
        {
            InternalVerbParameters mountInternal = new InternalVerbParameters(startedByService: true);
            return currentUser.RunAs(
                Configuration.Instance.GVFSLocation,
                $"mount {repoRoot} --{GVFSConstants.VerbParameters.InternalUseOnly} {mountInternal.ToJson()}");
        }
    }
}
