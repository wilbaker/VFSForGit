﻿using GVFS.Common.Tracing;

namespace GVFS.Service
{
    public interface IRepoMounter
    {
        bool MountRepository(string repoRoot, int sessionId, ITracer tracer);
        bool WaitUntilMounted(string repoRoot, bool unattended, out string errorMessage);
        string GetUserId(int sessionId);
    }
}
