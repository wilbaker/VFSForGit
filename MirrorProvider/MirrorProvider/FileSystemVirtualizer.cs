using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MirrorProvider
{
    public abstract class FileSystemVirtualizer
    {
        protected Enlistment enlistment;

        public abstract bool TryConvertVirtualizationRoot(string directory, out string error);
        public virtual bool TryStartVirtualizationInstance(Enlistment enlistment, out string error)
        {
            this.enlistment = enlistment;
            error = null;
            return true;
        }

        protected string GetFullPathInMirror(string relativePath)
        {
            return Path.Combine(this.enlistment.MirrorRoot, relativePath);
        }

        protected bool DirectoryExists(string relativePath)
        {
            string fullPathInMirror = this.GetFullPathInMirror(relativePath);
            DirectoryInfo dirInfo = new DirectoryInfo(fullPathInMirror);

            return dirInfo.Exists;
        }

        protected bool FileExists(string relativePath)
        {
            string fullPathInMirror = this.GetFullPathInMirror(relativePath);
            FileInfo fileInfo = new FileInfo(fullPathInMirror);

            return fileInfo.Exists;
        }

        protected ProjectedFileInfo GetFileInfo(string relativePath)
        {
            string fullPathInMirror = this.GetFullPathInMirror(relativePath);
            string fullParentPath = Path.GetDirectoryName(fullPathInMirror);
            string fileName = Path.GetFileName(relativePath);

            string actualCaseName;
            if (this.DirectoryExists(fullParentPath, fileName, out actualCaseName))
            {
                return new ProjectedFileInfo(actualCaseName, size: 0, type: ProjectedFileInfo.FileType.Directory);
            }
            else if (this.FileExists(fullParentPath, fileName, out actualCaseName))
            {
                // TODO: Check if the file is a symlink
                return new ProjectedFileInfo(actualCaseName, size: new FileInfo(fullPathInMirror).Length, type: ProjectedFileInfo.FileType.File);
            }

            return null;
        }

        protected IEnumerable<ProjectedFileInfo> GetChildItems(string relativePath)
        {
            string fullPathInMirror = this.GetFullPathInMirror(relativePath);
            DirectoryInfo dirInfo = new DirectoryInfo(fullPathInMirror);

            if (!dirInfo.Exists)
            {
                yield break;
            }

            foreach (FileInfo file in dirInfo.GetFiles())
            {
                // While not 100% accurate on all platforms, for simplicity assume that if the the file has reparse data it's a symlink
                yield return new ProjectedFileInfo(
                    file.Name, 
                    file.Length, 
                    type: (file.Attributes & FileAttributes.ReparsePoint) == FileAttributes.ReparsePoint ? 
                        ProjectedFileInfo.FileType.SymLink : 
                        ProjectedFileInfo.FileType.File);
            }

            foreach (DirectoryInfo subDirectory in dirInfo.GetDirectories())
            {
                yield return new ProjectedFileInfo(subDirectory.Name, size: 0, type: ProjectedFileInfo.FileType.Directory);
            }
        }

        protected FileSystemResult HydrateFile(string relativePath, int bufferSize, Func<byte[], uint, bool> tryWriteBytes)
        {
            string fullPathInMirror = this.GetFullPathInMirror(relativePath);
            if (!File.Exists(fullPathInMirror))
            {
                return FileSystemResult.EFileNotFound;
            }

            using (FileStream fs = new FileStream(fullPathInMirror, FileMode.Open, FileAccess.Read))
            {
                long remainingData = fs.Length;
                byte[] buffer = new byte[bufferSize];

                while (remainingData > 0)
                {
                    int bytesToCopy = (int)Math.Min(remainingData, buffer.Length);
                    if (fs.Read(buffer, 0, bytesToCopy) != bytesToCopy)
                    {
                        return FileSystemResult.EIOError;
                    }

                    if (!tryWriteBytes(buffer, (uint)bytesToCopy))
                    {
                        return FileSystemResult.EIOError;
                    }

                    remainingData -= bytesToCopy;
                }
            }

            return FileSystemResult.Success;
        }

        private bool DirectoryExists(string fullParentPath, string directoryName, out string actualCaseName)
        {
            return this.NameExists(Directory.GetDirectories(fullParentPath), directoryName, out actualCaseName);
        }

        private bool FileExists(string fullParentPath, string fileName, out string actualCaseName)
        {
            return this.NameExists(Directory.GetFiles(fullParentPath), fileName, out actualCaseName);
        }

        private bool NameExists(IEnumerable<string> paths, string name, out string actualCaseName)
        {
            actualCaseName = 
                paths
                .Select(path => Path.GetFileName(path))
                .FirstOrDefault(actualName => actualName.Equals(name, StringComparison.OrdinalIgnoreCase));
            return actualCaseName != null;
        }
    }
}
