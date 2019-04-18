using GVFS.Common;
using GVFS.Common.Tracing;
using GVFS.Platform.Windows;
using GVFS.Service.Handlers;
using System;

namespace GVFS.Service
{
    public class GVFSMountProcess : IRepoMountProcess
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
                if (!this.WaitUntilMounted(repoRoot, false, out errorMessage))
                {
                    tracer.RelatedError(errorMessage);
                    currentUser.Dispose();
                    return false;
                }
            }

            return true;
        }

        public bool WaitUntilMounted(string repoRoot, bool unattended, out string errorMessage)
        {
            return GVFSEnlistment.WaitUntilMounted(repoRoot, unattended, out errorMessage);
        }

        public string GetUserId(int sessionId)
        {
            using (CurrentUser currentUser = new CurrentUser(tracer: null, sessionId: sessionId))
            {
                return currentUser.Identity.User.Value;
            }
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
