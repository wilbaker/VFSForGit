﻿using GVFS.Common;
using GVFS.Common.Tracing;
using GVFS.PlatformLoader;
using System;

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
                GVFSService.CreateAndRun(tracer, args);
            }
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
