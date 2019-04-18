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
        private const string ServiceNameArgPrefix = "--servicename=";
        private const string EtwArea = nameof(GVFSService);

        private ITracer tracer;
        private Thread serviceThread;
        private ManualResetEvent serviceStopped;
        private string serviceName;
        private bool startListening;
        private RepoRegistry repoRegistry;
        private RequestHandler requestHandler;
        private GVFSPlatform gvfsPlatform;

        public GVFSService(
            ITracer tracer,
            string serviceName,
            bool startListening,
            RepoRegistry repoRegistry,
            GVFSPlatform gvfsPlatform)
        {
            this.tracer = tracer;
            this.repoRegistry = repoRegistry;
            this.gvfsPlatform = gvfsPlatform;
            this.serviceName = serviceName;
            this.startListening = startListening;

            this.serviceStopped = new ManualResetEvent(false);
            this.serviceThread = new Thread(this.ServiceThreadMain);
            this.requestHandler = new RequestHandler(this.tracer, EtwArea, this.repoRegistry);
        }

        public static GVFSService CreateService(JsonTracer tracer, string[] args)
        {
            string serviceName = args.FirstOrDefault(arg => arg.StartsWith(ServiceNameArgPrefix));
            if (serviceName != null)
            {
                serviceName = serviceName.Substring(ServiceNameArgPrefix.Length);
            }
            else
            {
                serviceName = GVFSConstants.Service.ServiceName;
            }

            GVFSPlatform gvfsPlatform = GVFSPlatform.Instance;

            string logFilePath = Path.Combine(
                    gvfsPlatform.GetDataRootForGVFSComponent(serviceName),
                    GVFSConstants.Service.LogDirectory);
            Directory.CreateDirectory(logFilePath);

            tracer.AddLogFileEventListener(
                GVFSEnlistment.GetNewGVFSLogFileName(logFilePath, GVFSConstants.LogFileTypes.Service),
                EventLevel.Verbose,
                Keywords.Any);

            string serviceDataLocation = gvfsPlatform.GetDataRootForGVFSComponent(serviceName);
            RepoRegistry repoRegistry = new RepoRegistry(
                tracer,
                new PhysicalFileSystem(),
                serviceDataLocation,
                gvfsPlatform,
                new GVFSMountProcess());

            return new GVFSService(
                tracer,
                serviceName,
                startListening: true,
                repoRegistry: repoRegistry,
                gvfsPlatform: gvfsPlatform);
        }

        public void RunWithArgs(string[] args)
        {
            try
            {
                this.RunRepoRegistryTasks();

                if (this.startListening && !string.IsNullOrEmpty(this.serviceName))
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
                this.LogExceptionAndExit(e, nameof(this.RunWithArgs));
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

        private void RunRepoRegistryTasks()
        {
            string currentUser = this.gvfsPlatform.GetCurrentUser();

            this.repoRegistry.AutoMountRepos(int.Parse(currentUser));
            this.repoRegistry.TraceStatus();
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
