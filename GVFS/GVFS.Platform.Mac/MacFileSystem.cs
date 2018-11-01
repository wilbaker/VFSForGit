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
            StatBuffer statBuffer = new StatBuffer();
            if (Stat(fileName, out statBuffer) == 0)
            {
                return true;
            }

            return false;
        }

        public bool IsSocket(string fileName)
        {
            StatBuffer statBuffer = new StatBuffer();
            if (Stat(fileName, out statBuffer) == 0)
            {
                return true;
            }

            return false;
        }

        [DllImport("libc", EntryPoint = "chmod", SetLastError = true)]
        private static extern int Chmod(string pathname, int mode);

        [DllImport("libc", EntryPoint = "rename", SetLastError = true)]
        private static extern int Rename(string oldPath, string newPath);

        [DllImport("libc", EntryPoint = "stat", SetLastError = true)]
        private static extern int Stat(string path, [Out] out StatBuffer statBuffer);

        [StructLayout(LayoutKind.Sequential)]
        private struct TimeSpec
        {
            public long Sec;
            public long Nsec;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct StatBuffer
        {
            public int Dev;              /* ID of device containing file */
            public ushort Mode;          /* Mode of file (see below) */
            public ushort NLink;         /* Number of hard links */
            public ulong Ino;            /* File serial number */
            public uint UID;             /* User ID of the file */
            public uint GID;             /* Group ID of the file */
            public int RDev;             /* Device ID */

            public TimeSpec ATimespec;     /* time of last access */
            public TimeSpec MTimespec;     /* time of last data modification */
            public TimeSpec CTimespec;     /* time of last status change */
            public TimeSpec BirthTimespec; /* time of file creation(birth) */

            public long Size;          /* file size, in bytes */
            public long Blocks;        /* blocks allocated for file */
            public int BlkSize;        /* optimal blocksize for I/O */
            public uint Glags;         /* user defined flags for file */
            public uint Gen;           /* file generation number */
            public int LSpare;         /* RESERVED: DO NOT USE! */

            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
            public long[] QSpare;     /* RESERVED: DO NOT USE! */
        }

        private static class NativeFileReader
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
        }
    }
}
