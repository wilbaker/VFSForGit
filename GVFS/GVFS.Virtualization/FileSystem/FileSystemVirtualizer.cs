﻿using GVFS.Common;
using GVFS.Common.Git;
using GVFS.Common.Tracing;
using GVFS.Virtualization.BlobSize;
using System;
using System.Collections.Concurrent;
using System.Text;
using System.Threading;

namespace GVFS.Virtualization.FileSystem
{
    public abstract class FileSystemVirtualizer : IDisposable
    {
        public const byte PlaceholderVersion = 1;

        protected static readonly GitCommandLineParser.Verbs CanCreatePlaceholderVerbs =
            GitCommandLineParser.Verbs.AddOrStage | GitCommandLineParser.Verbs.Move | GitCommandLineParser.Verbs.Status;

        private BlockingCollection<FileOrNetworkRequest> fileAndNetworkRequests;
        private Thread[] fileAndNetworkWorkerThreads;
        private int numWorkerThreads;

        protected FileSystemVirtualizer(GVFSContext context, GVFSGitObjects gvfsGitObjects)
            : this(context, gvfsGitObjects, FileSystemVirtualizer.DefaultNumWorkerThreads)
        {
        }

        protected FileSystemVirtualizer(GVFSContext context, GVFSGitObjects gvfsGitObjects, int numWorkerThreads)
        {
            if (numWorkerThreads <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(numWorkerThreads), numWorkerThreads, "Number of threads must be greater than 0");
            }

            this.Context = context;
            this.GitObjects = gvfsGitObjects;
            this.fileAndNetworkRequests = new BlockingCollection<FileOrNetworkRequest>();

            this.numWorkerThreads = numWorkerThreads;
        }

        protected static int DefaultNumWorkerThreads
        {
            get
            {
                return Environment.ProcessorCount;
            }
        }

        protected GVFSContext Context { get; private set; }
        protected GVFSGitObjects GitObjects { get; private set; }
        protected FileSystemCallbacks FileSystemCallbacks { get; private set; }
        protected virtual string EtwArea
        {
            get
            {
                return nameof(FileSystemVirtualizer);
            }
        }

        public static byte[] ConvertShaToContentId(string sha)
        {
            return Encoding.Unicode.GetBytes(sha);
        }

        /// <summary>
        /// GVFS uses the first byte of the providerId field of placeholders to version
        /// the data that it stores in the contentId (and providerId) fields of the placeholder
        /// </summary>
        /// <returns>Byte array to set as placeholder version Id</returns>
        public static byte[] GetPlaceholderVersionId()
        {
            return new byte[] { PlaceholderVersion };
        }

        public virtual bool TryStart(FileSystemCallbacks fileSystemCallbacks, out string error)
        {
            this.FileSystemCallbacks = fileSystemCallbacks;

            this.fileAndNetworkWorkerThreads = new Thread[this.numWorkerThreads];
            for (int i = 0; i < this.fileAndNetworkWorkerThreads.Length; ++i)
            {
                this.fileAndNetworkWorkerThreads[i] = new Thread(this.ExecuteFileOrNetworkRequest);
                this.fileAndNetworkWorkerThreads[i].IsBackground = true;
                this.fileAndNetworkWorkerThreads[i].Start();
            }

            return this.TryStart(out error);
        }

        public void PrepareToStop()
        {
            this.fileAndNetworkRequests.CompleteAdding();
            foreach (Thread t in this.fileAndNetworkWorkerThreads)
            {
                t.Join();
            }
        }

        public abstract void Stop();

        public abstract FileSystemResult ClearNegativePathCache(out uint totalEntryCount);

        public abstract FileSystemResult DeleteFile(string relativePath, UpdatePlaceholderType updateFlags, out UpdateFailureReason failureReason);

        public abstract FileSystemResult UpdatePlaceholderIfNeeded(
            string relativePath,
            DateTime creationTime,
            DateTime lastAccessTime,
            DateTime lastWriteTime,
            DateTime changeTime,
            uint fileAttributes,
            long endOfFile,
            string shaContentId,
            UpdatePlaceholderType updateFlags,
            out UpdateFailureReason failureReason);

        public void Dispose()
        {
            if (this.fileAndNetworkRequests != null)
            {
                this.fileAndNetworkRequests.Dispose();
                this.fileAndNetworkRequests = null;
            }
        }

        protected static string GetShaFromContentId(byte[] contentId)
        {
            return Encoding.Unicode.GetString(contentId, 0, GVFSConstants.ShaStringLength * sizeof(char));
        }

        protected static byte GetPlaceholderVersionFromProviderId(byte[] providerId)
        {
            return providerId[0];
        }

        protected abstract bool TryStart(out string error);

        /// <remarks>
        /// If a git-status or git-add is running, we don't want to fail placeholder creation because users will
        /// want to be able to run those commands during long running builds. Allow lock acquisition to be deferred
        /// until background thread actually needs it.
        /// 
        /// git-mv is also allowed to defer since it needs to create the files it moves.
        /// </remarks>
        protected bool CanCreatePlaceholder()
        {
            GitCommandLineParser gitCommand = new GitCommandLineParser(this.Context.Repository.GVFSLock.GetLockedGitCommand());
            return
                !gitCommand.IsValidGitCommand ||
                gitCommand.IsVerb(FileSystemVirtualizer.CanCreatePlaceholderVerbs);
        }

        protected bool IsSpecialGitFile(string fileName)
        {
            return
                fileName.Equals(GVFSConstants.SpecialGitFiles.GitAttributes, StringComparison.OrdinalIgnoreCase) ||
                fileName.Equals(GVFSConstants.SpecialGitFiles.GitIgnore, StringComparison.OrdinalIgnoreCase);
        }

        protected void OnDotGitFileOrFolderChanged(string relativePath)
        {
            if (relativePath.Equals(GVFSConstants.DotGit.Index, StringComparison.OrdinalIgnoreCase))
            {
                this.FileSystemCallbacks.OnIndexFileChange();
            }
            else if (relativePath.Equals(GVFSConstants.DotGit.Logs.Head, StringComparison.OrdinalIgnoreCase))
            {
                this.FileSystemCallbacks.OnLogsHeadChange();
            }
            else if (IsPathHeadOrLocalBranch(relativePath))
            {
                this.FileSystemCallbacks.OnHeadOrRefChanged();
            }
            else if (relativePath.Equals(GVFSConstants.DotGit.Info.ExcludePath, StringComparison.OrdinalIgnoreCase))
            {
                this.FileSystemCallbacks.OnExcludeFileChanged();
            }
        }

        protected void OnDotGitFileOrFolderDeleted(string relativePath)
        {
            if (IsPathHeadOrLocalBranch(relativePath))
            {
                this.FileSystemCallbacks.OnHeadOrRefChanged();
            }
            else if (relativePath.Equals(GVFSConstants.DotGit.Info.ExcludePath, StringComparison.OrdinalIgnoreCase))
            {
                this.FileSystemCallbacks.OnExcludeFileChanged();
            }
        }

        protected void OnWorkingDirectoryFileOrFolderDeleted(string relativePath, bool isDirectory)
        {
            if (isDirectory)
            {
                // Don't want to add folders to the modified list if git is the one deleting the directory
                GitCommandLineParser gitCommand = new GitCommandLineParser(this.Context.Repository.GVFSLock.GetLockedGitCommand());
                if (!gitCommand.IsValidGitCommand)
                {
                    this.FileSystemCallbacks.OnFolderDeleted(relativePath);
                }
            }
            else
            {
                this.FileSystemCallbacks.OnFileDeleted(relativePath);
            }

            this.FileSystemCallbacks.InvalidateGitStatusCache();
        }
        
        protected void OnFileRenamed(string relativeSourcePath, string relativeDestinationPath, bool isDirectory)
        {
            try
            {
                bool srcPathInDotGit = FileSystemCallbacks.IsPathInsideDotGit(relativeSourcePath);
                bool dstPathInDotGit = FileSystemCallbacks.IsPathInsideDotGit(relativeDestinationPath);

                if (dstPathInDotGit)
                {
                    this.OnDotGitFileOrFolderChanged(relativeDestinationPath);
                }

                if (!(srcPathInDotGit && dstPathInDotGit))
                {
                    if (isDirectory)
                    {
                        this.FileSystemCallbacks.OnFolderRenamed(relativeSourcePath, relativeDestinationPath);
                    }
                    else
                    {
                        this.FileSystemCallbacks.OnFileRenamed(relativeSourcePath, relativeDestinationPath);
                    }
                }
            }
            catch (Exception e)
            {
                EventMetadata metadata = this.CreateEventMetadata(relativeSourcePath, e);
                metadata.Add("destinationPath", relativeDestinationPath);
                metadata.Add("isDirectory", isDirectory);
                this.LogUnhandledExceptionAndExit(nameof(this.OnFileRenamed), metadata);
            }
        }

        protected void OnHardLinkCreated(string relativeTargetPath, string relativeNewLinkPath)
        {
            try
            {
                bool pathInDotGit = FileSystemCallbacks.IsPathInsideDotGit(relativeNewLinkPath);

                if (pathInDotGit)
                {
                    this.OnDotGitFileOrFolderChanged(relativeNewLinkPath);
                }
                else
                {
                    this.FileSystemCallbacks.OnFileHardLinkCreated(relativeNewLinkPath);
                }
            }
            catch (Exception e)
            {
                EventMetadata metadata = this.CreateEventMetadata(relativeNewLinkPath, e);
                metadata.Add(nameof(relativeTargetPath), relativeTargetPath);
                this.LogUnhandledExceptionAndExit(nameof(this.OnHardLinkCreated), metadata);
            }
        }

        protected EventMetadata CreateEventMetadata(
            Guid enumerationId,
            string relativePath = null,
            Exception exception = null)
        {
            EventMetadata metadata = this.CreateEventMetadata(relativePath, exception);
            metadata.Add("enumerationId", enumerationId);
            return metadata;
        }

        protected EventMetadata CreateEventMetadata(
            string relativePath = null,
            Exception exception = null)
        {
            EventMetadata metadata = new EventMetadata();
            metadata.Add("Area", this.EtwArea);

            if (relativePath != null)
            {
                metadata.Add(nameof(relativePath), relativePath);
            }

            if (exception != null)
            {
                metadata.Add("Exception", exception.ToString());
            }

            return metadata;
        }

        protected bool TryScheduleFileOrNetworkRequest(FileOrNetworkRequest request, out Exception exception)
        {
            exception = null;

            try
            {
                this.fileAndNetworkRequests.Add(request);
                return true;
            }
            catch (InvalidOperationException e)
            {
                // Attempted to call Add after CompleteAdding has been called
                exception = e;
            }

            return false;
        }

        protected void LogUnhandledExceptionAndExit(string methodName, EventMetadata metadata)
        {
            this.Context.Tracer.RelatedError(metadata, methodName + " caught unhandled exception, exiting process");
            Environment.Exit(1);
        }

        private static bool IsPathHeadOrLocalBranch(string relativePath)
        {
            if (!relativePath.EndsWith(GVFSConstants.DotGit.LockExtension, StringComparison.OrdinalIgnoreCase) &&
                (relativePath.Equals(GVFSConstants.DotGit.Head, StringComparison.OrdinalIgnoreCase) ||
                relativePath.StartsWith(GVFSConstants.DotGit.Refs.Heads.RootFolder, StringComparison.OrdinalIgnoreCase)))
            {
                return true;
            }

            return false;
        }

        private void ExecuteFileOrNetworkRequest()
        {
            try
            {
                using (BlobSizes.BlobSizesConnection blobSizesConnection = this.FileSystemCallbacks.BlobSizes.CreateConnection())
                {
                    FileOrNetworkRequest request;
                    while (this.fileAndNetworkRequests.TryTake(out request, Timeout.Infinite))
                    {
                        try
                        {
                            request.Work(blobSizesConnection);
                        }
                        catch (Exception e)
                        {
                            EventMetadata metadata = this.CreateEventMetadata(relativePath: null, exception: e);
                            this.LogUnhandledExceptionAndExit($"{nameof(this.ExecuteFileOrNetworkRequest)}_Work", metadata);
                        }

                        try
                        {
                            request.Cleanup();
                        }
                        catch (Exception e)
                        {
                            EventMetadata metadata = this.CreateEventMetadata(relativePath: null, exception: e);
                            this.LogUnhandledExceptionAndExit($"{nameof(this.ExecuteFileOrNetworkRequest)}_Cleanup", metadata);
                        }
                    }
                }
            }
            catch (Exception e)
            {
                EventMetadata metadata = this.CreateEventMetadata(relativePath: null, exception: e);
                this.LogUnhandledExceptionAndExit($"{nameof(this.ExecuteFileOrNetworkRequest)}", metadata);
            }
        }

        /// <summary>
        /// Requests from the file system that require file and\or network access (and hence
        /// should be executed asynchronously).
        /// </summary>
        protected class FileOrNetworkRequest
        {
            /// <summary>
            /// FileOrNetworkRequest constructor 
            /// </summary>
            /// <param name="work">Action that requires file and\or network access</param>
            /// <param name="cleanup">Cleanup action to take after performing work</param>
            public FileOrNetworkRequest(Action<BlobSizes.BlobSizesConnection> work, Action cleanup)
            {
                this.Work = work;
                this.Cleanup = cleanup;
            }

            public Action<BlobSizes.BlobSizesConnection> Work { get; }
            public Action Cleanup { get; }
        }
    }
}
