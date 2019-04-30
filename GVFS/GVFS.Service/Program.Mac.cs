﻿using GVFS.Common;
using GVFS.Common.FileSystem;
using GVFS.Common.Tracing;
using GVFS.PlatformLoader;
using System;
using System.IO;
using System.Linq;

namespace GVFS.Service
{
    public static class Program
    {
        public static void Main(string[] args)
        {
            GVFSPlatformLoader.Initialize();

            AppDomain.CurrentDomain.UnhandledException += UnhandledExceptionHandler;

            using (JsonTracer tracer = new JsonTracer(GVFSConstants.Service.ServiceName, GVFSConstants.Service.ServiceName))
            {
                CreateService(tracer, args).Run();
            }
        }

        private static MacGVFSService CreateService(JsonTracer tracer, string[] args)
        {
            string serviceName = args.FirstOrDefault(arg => arg.StartsWith(MacGVFSService.ServiceNameArgPrefix, StringComparison.OrdinalIgnoreCase));
            if (serviceName != null)
            {
                serviceName = serviceName.Substring(MacGVFSService.ServiceNameArgPrefix.Length);
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
                new MacGVFSMountProcess(tracer));

            return new MacGVFSService(tracer, serviceName, repoRegistry, gvfsPlatform);
        }

        private static void UnhandledExceptionHandler(object sender, UnhandledExceptionEventArgs e)
        {
            using (JsonTracer tracer = new JsonTracer(GVFSConstants.Service.ServiceName, GVFSConstants.Service.ServiceName))
            {
                tracer.RelatedError($"Unhandled exception in GVFS.Service: {e.ExceptionObject.ToString()}");
            }
        }
    }
}
