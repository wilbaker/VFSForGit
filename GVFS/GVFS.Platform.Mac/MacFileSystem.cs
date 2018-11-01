using GVFS.Common;
using GVFS.Common.FileSystem;
using System.IO;
using System.Runtime.InteropServices;

namespace GVFS.Platform.Mac
{
    public partial class MacFileSystem : IPlatformFileSystem
    {
        public bool SupportsFileMode { get; } = true;

        public void FlushFileBuffers(string path)
        {
            // TODO(Mac): Use native API to flush file
        }

        public void MoveAndOverwriteFile(string sourceFileName, string destinationFilename)
        {
            if (Rename(sourceFileName, destinationFilename) != 0)
            {
                NativeMethods.ThrowLastWin32Exception($"Failed to renname {sourceFileName} to {destinationFilename}");
            }
        }

        public void CreateHardLink(string newFileName, string existingFileName)
        {
            // TODO(Mac): Use native API to create a hardlink
            File.Copy(existingFileName, newFileName);
        }

        public void ChangeMode(string path, int mode)
        {
           Chmod(path, mode);
        }

        public bool TryGetNormalizedPath(string path, out string normalizedPath, out string errorMessage)
        {
            return MacFileSystem.TryGetNormalizedPathImplementation(path, out normalizedPath, out errorMessage);
        }

        public bool HydrateFile(string fileName, byte[] buffer)
        {
            return NativeFileReader.TryReadFirstByteOfFile(fileName, buffer);
        }
        public bool IsExecutable(string fileName)
        {
            if (!File.Exists(fileName))
            {
                return false;
            }
        }

        public bool IsSocket(string fileName)
        {
            
        }

        [DllImport("libc", EntryPoint = "chmod", SetLastError = true)]
        private static extern int Chmod(string pathname, int mode);

        [DllImport("libc", EntryPoint = "rename", SetLastError = true)]
        private static extern int Rename(string oldPath, string newPath);

        private class NativeFileReader
        {
            private const int ReadOnly = 0x0000;

            public static bool TryReadFirstByteOfFile(string fileName, byte[] buffer)
            {
                int fileDescriptor = 1;
                bool readStatus;
                try
                {
                    fileDescriptor = Open(fileName, ReadOnly);
                    readStatus = TryReadOneByte(fileDescriptor, buffer);
                }
                finally
                {
                    Close(fileDescriptor);
                }

                return readStatus;
            }

            private static bool TryReadOneByte(int fileDescriptor, byte[] buffer)
            {
                int numBytes = Read(fileDescriptor, buffer, 1);

                if (numBytes == -1)
                {
                    return false;
                }

                return true;
            }

            [DllImport("libc", EntryPoint = "open", SetLastError = true)]
            private static extern int Open(string path, int flag);

            [DllImport("libc", EntryPoint = "close", SetLastError = true)]
            private static extern int Close(int fd);

            [DllImport("libc", EntryPoint = "read", SetLastError = true)]
            private static extern int Read(int fd, [Out] byte[] buf, int count);

            [DllImport("libc", EntryPoint = "stat", SetLastError = true)]
            private static extern int Stat(string path, [Out] out StatBuffer statBuffer);

            [StructLayout(LayoutKind.Sequential)]
            private struct TimeSpec
            {
                public long tv_sec;
                public long tv_nsec;
            }

            [StructLayout(LayoutKind.Sequential)]
            private struct StatBuffer
            { 
                int st_dev;              /* ID of device containing file */
                ushort st_mode;          /* Mode of file (see below) */
                ushort st_nlink;         /* Number of hard links */
                ulong st_ino;            /* File serial number */
                uint st_uid;             /* User ID of the file */
                uint st_gid;             /* Group ID of the file */
                int st_rdev;             /* Device ID */

                TimeSpec st_atimespec;     /* time of last access */
                TimeSpec st_mtimespec;     /* time of last data modification */
                TimeSpec st_ctimespec;     /* time of last status change */
                TimeSpec st_birthtimespec; /* time of file creation(birth) */

                long st_size;          /* file size, in bytes */
                long st_blocks;        /* blocks allocated for file */
                int st_blksize;        /* optimal blocksize for I/O */
                uint st_flags;         /* user defined flags for file */
                uint st_gen;           /* file generation number */
                int st_lspare;         /* RESERVED: DO NOT USE! */

                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
                long[] st_qspare;     /* RESERVED: DO NOT USE! */
            };
        }
    }
}
