using GVFS.Common;
using GVFS.Common.FileSystem;
using GVFS.Common.NamedPipes;
using GVFS.Common.Tracing;
using GVFS.Service.Handlers;
using System;
using System.IO;
using System.Linq;
using System.Threading;

namespace GVFS.Service
{
    public class GVFSService
    {
        public const string ServiceNameArgPrefix = "--servicename=";

        private const string EtwArea = nameof(GVFSService);

        private ITracer tracer;
        private Thread serviceThread;
        private ManualResetEvent serviceStopped;
        private string serviceName;
        private IRepoRegistry repoRegistry;
        private RequestHandler requestHandler;
        private GVFSPlatform gvfsPlatform;

        public GVFSService(
            ITracer tracer,
            string serviceName,
            IRepoRegistry repoRegistry,
            GVFSPlatform gvfsPlatform)
        {
            this.tracer = tracer;
            this.repoRegistry = repoRegistry;
            this.gvfsPlatform = gvfsPlatform;
            this.serviceName = serviceName;

            this.serviceStopped = new ManualResetEvent(false);
            this.serviceThread = new Thread(this.ServiceThreadMain);
            this.requestHandler = new RequestHandler(this.tracer, EtwArea, this.repoRegistry);
        }

        public void Run()
        {
            try
            {
                this.AutoMountReposForUser();

                if (!string.IsNullOrEmpty(this.serviceName))
                {
                    string pipeName = this.serviceName + ".Pipe";
                    this.tracer.RelatedInfo("Starting pipe server with name: " + pipeName);

                    using (NamedPipeServer pipeServer = NamedPipeServer.StartNewServer(
                        pipeName,
                        this.tracer,
                        this.requestHandler.HandleRequest))
                    {
                        this.serviceThread.Start();
                        this.serviceThread.Join();
                    }
                }
                else
                {
                    this.tracer.RelatedError("No name specified for Service Pipe.");
                }
            }
            catch (Exception e)
            {
                this.LogExceptionAndExit(e, nameof(this.Run));
            }
        }

        private void ServiceThreadMain()
        {
            try
            {
                EventMetadata metadata = new EventMetadata();
                metadata.Add("Version", ProcessHelper.GetCurrentProcessVersion());
                this.tracer.RelatedEvent(EventLevel.Informational, $"{nameof(GVFSService)}_{nameof(this.ServiceThreadMain)}", metadata);

                this.serviceStopped.WaitOne();
                this.serviceStopped.Dispose();
            }
            catch (Exception e)
            {
                this.LogExceptionAndExit(e, nameof(this.ServiceThreadMain));
            }
        }

        private void AutoMountReposForUser()
        {
            int currentUser;
            if (int.TryParse(this.gvfsPlatform.GetCurrentUser(), out currentUser))
            {
                // On Mac, there is no separate session Id. currentUser is used as sessionId
                this.repoRegistry.AutoMountRepos(currentUser.ToString(), currentUser);
                this.repoRegistry.TraceStatus();
            }
            else
            {
                this.tracer.RelatedError($"{nameof(this.AutoMountReposForUser)} Error: could not parse current user info from RepoRegistry.");
            }
        }

        private void LogExceptionAndExit(Exception e, string method)
        {
            EventMetadata metadata = new EventMetadata();
            metadata.Add("Area", EtwArea);
            metadata.Add("Exception", e.ToString());
            this.tracer.RelatedError(metadata, "Unhandled exception in " + method);
            Environment.Exit((int)ReturnCode.GenericError);
        }
    }
}
