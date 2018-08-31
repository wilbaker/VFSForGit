﻿using GVFS.Common.FileSystem;
using System;

namespace GVFS.UnitTests.Mock.FileSystem
{
    public class MockPlatformFileSystem : IPlatformFileSystem
    {
        public bool SupportsFileMode { get; } = true;
        public bool EnumerationExpandsDirectories
        {
            get { throw new NotSupportedException(); }
        }

        public void FlushFileBuffers(string path)
        {
            throw new NotSupportedException();
        }

        public void MoveAndOverwriteFile(string sourceFileName, string destinationFilename)
        {
            throw new NotSupportedException();
        }

        public void CreateHardLink(string newFileName, string existingFileName)
        {
            throw new NotSupportedException();
        }

        public void ChangeMode(string path, int mode)
        {
            throw new NotSupportedException();
        }

        public bool TryGetNormalizedPath(string path, out string normalizedPath, out string errorMessage)
        {
            errorMessage = null;
            normalizedPath = path;
            return true;
        }
    }
}
