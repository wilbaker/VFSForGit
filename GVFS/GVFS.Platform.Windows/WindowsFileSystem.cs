﻿using GVFS.Common;
using GVFS.Common.FileSystem;
using GVFS.Common.Tracing;
using Microsoft.Win32.SafeHandles;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;

namespace GVFS.Platform.Windows
{
    public partial class WindowsFileSystem : IPlatformFileSystem
    {
        public bool SupportsFileMode { get; } = false;

        /// <summary>
        /// Adds a new FileSystemAccessRule granting read (and optionally modify) access for all users.
        /// </summary>
        /// <param name="directorySecurity">DirectorySecurity to which a FileSystemAccessRule will be added.</param>
        /// <param name="grantUsersModifyPermissions">
        /// True if all users should be given modify access, false if users should only be allowed read access
        /// </param>
        public static void AddUsersAccessRulesToDirectorySecurity(DirectorySecurity directorySecurity, bool grantUsersModifyPermissions)
        {
            SecurityIdentifier allUsers = new SecurityIdentifier(WellKnownSidType.BuiltinUsersSid, null);
            FileSystemRights rights = FileSystemRights.Read;
            if (grantUsersModifyPermissions)
            {
                rights = rights | FileSystemRights.Modify;
            }

            // InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit -> ACE is inherited by child directories and files
            // PropagationFlags.None -> Standard propagation rules, settings are applied to the directory and its children
            // AccessControlType.Allow -> Rule is used to allow access to an object
            directorySecurity.AddAccessRule(
                new FileSystemAccessRule(
                    allUsers,
                    rights,
                    InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                    PropagationFlags.None,
                    AccessControlType.Allow));
        }

        /// <summary>
        /// Adds a new FileSystemAccessRule granting read/exceute/modify/delete access for administrators.
        /// </summary>
        /// <param name="directorySecurity">DirectorySecurity to which a FileSystemAccessRule will be added.</param>
        public static void AddAdminAccessRulesToDirectorySecurity(DirectorySecurity directorySecurity)
        {
            SecurityIdentifier administratorUsers = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);

            // InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit -> ACE is inherited by child directories and files
            // PropagationFlags.None -> Standard propagation rules, settings are applied to the directory and its children
            // AccessControlType.Allow -> Rule is used to allow access to an object
            directorySecurity.AddAccessRule(
                new FileSystemAccessRule(
                    administratorUsers,
                    FileSystemRights.ReadAndExecute | FileSystemRights.Modify | FileSystemRights.Delete,
                    InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                    PropagationFlags.None,
                    AccessControlType.Allow));
        }

        /// <summary>
        /// Removes all FileSystemAccessRules from specified DirectorySecurity
        /// </summary>
        /// <param name="directorySecurity">DirectorySecurity from which to remove FileSystemAccessRules</param>
        public static void RemoveAllFileSystemAccessRulesFromDirectorySecurity(DirectorySecurity directorySecurity)
        {
            AuthorizationRuleCollection currentRules = directorySecurity.GetAccessRules(includeExplicit: true, includeInherited: true, targetType: typeof(NTAccount));
            foreach (AuthorizationRule authorizationRule in currentRules)
            {
                FileSystemAccessRule fileSystemRule = authorizationRule as FileSystemAccessRule;
                if (fileSystemRule != null)
                {
                    directorySecurity.RemoveAccessRule(fileSystemRule);
                }
            }
        }

        public void FlushFileBuffers(string path)
        {
            NativeMethods.FlushFileBuffers(path);
        }

        public void MoveAndOverwriteFile(string sourceFileName, string destinationFilename)
        {
            NativeMethods.MoveFile(
                sourceFileName,
                destinationFilename,
                NativeMethods.MoveFileFlags.MoveFileReplaceExisting);
        }

        public void ChangeMode(string path, ushort mode)
        {
        }

        public bool TryGetNormalizedPath(string path, out string normalizedPath, out string errorMessage)
        {
            return WindowsFileSystem.TryGetNormalizedPathImplementation(path, out normalizedPath, out errorMessage);
        }

        public bool HydrateFile(string fileName, byte[] buffer)
        {
            return NativeFileReader.TryReadFirstByteOfFile(fileName, buffer);
        }

        public bool IsExecutable(string fileName)
        {
            string fileExtension = Path.GetExtension(fileName);
            return string.Equals(fileExtension, ".exe", GVFSPlatform.Instance.Constants.PathComparison);
        }

        public bool IsSocket(string fileName)
        {
            return false;
        }

        public void CreateDirectoryAccessibleByAuthUsers(string directoryPath)
        {
            if (Directory.Exists(directoryPath))
            {
                return;
            }

            // Find the closest ancestor that exists on disk.
            string parentPath = directoryPath;
            while (!string.IsNullOrWhiteSpace(parentPath) && !Directory.Exists(parentPath))
            {
                parentPath = Path.GetDirectoryName(parentPath);
            }

            if (string.IsNullOrWhiteSpace(parentPath))
            {
                throw new DirectoryNotFoundException($"Failed to find an ancestor of {directoryPath} on disk");
            }

            // Create a temporary directory and read its ACLs.  We do this for two reasons:
            //
            // 1) The call to Directory.CreateDirectory below must be made with both the
            //    proper inherited ACLs, and the ACLs we want to add
            //
            // 2) Setting the ACLs *after* creating the directory is tricky because CreateDirectory
            //    might need to create intermediate directories, and we needs those to have the correct ACLs as well
            string tempDir = Path.Combine(parentPath, $"gvfs_{Path.GetRandomFileName()}");
            Directory.CreateDirectory(tempDir);
            DirectorySecurity directorySecurity;
            try
            {
                directorySecurity = Directory.GetAccessControl(tempDir);
            }
            finally
            {
                Directory.Delete(tempDir);
            }

            // The following permissions are typically present on deskop and missing on Server
            //
            //   ACCESS_ALLOWED_ACE_TYPE: NT AUTHORITY\Authenticated Users
            //          [OBJECT_INHERIT_ACE]
            //          [CONTAINER_INHERIT_ACE]
            //          [INHERIT_ONLY_ACE]
            //        DELETE
            //        GENERIC_EXECUTE
            //        GENERIC_WRITE
            //        GENERIC_READ

            // Use AccessRuleFactory rather than a FileSystemAccessRule because the NativeMethods.FileAccess flags we're specifying
            // are not valid for the FileSystemRights parameter of the FileSystemAccessRule constructor
            AccessRule authenticatedUsersAccessRule = directorySecurity.AccessRuleFactory(
                new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid, null),
                unchecked((int)(NativeMethods.FileAccess.DELETE | NativeMethods.FileAccess.GENERIC_EXECUTE | NativeMethods.FileAccess.GENERIC_WRITE | NativeMethods.FileAccess.GENERIC_READ)),
                true,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                PropagationFlags.None,
                AccessControlType.Allow);

            // The return type of the AccessRuleFactory method is the base class, AccessRule, but the return value can be cast safely to the derived class.
            // https://msdn.microsoft.com/en-us/library/system.security.accesscontrol.filesystemsecurity.accessrulefactory(v=vs.110).aspx
            directorySecurity.AddAccessRule((FileSystemAccessRule)authenticatedUsersAccessRule);

            Directory.CreateDirectory(directoryPath, directorySecurity);
        }

        public bool TryCreateDirectoryWithAdminAndUserModifyPermissions(string directoryPath, out string error)
        {
            try
            {
                DirectorySecurity directorySecurity = new DirectorySecurity();

                // Protect the access rules from inheritance and remove any inherited rules
                directorySecurity.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);

                // Add new ACLs for users and admins.  Users will be granted write permissions.
                AddUsersAccessRulesToDirectorySecurity(directorySecurity, grantUsersModifyPermissions: true);
                AddAdminAccessRulesToDirectorySecurity(directorySecurity);

                Directory.CreateDirectory(directoryPath, directorySecurity);
            }
            catch (Exception e) when (e is IOException ||
                                      e is UnauthorizedAccessException ||
                                      e is PathTooLongException ||
                                      e is DirectoryNotFoundException)
            {
                error = $"Exception while creating directory `{directoryPath}`: {e.Message}";
                return false;
            }

            error = null;
            return true;
        }

        public bool TryCreateOrUpdateDirectoryToAdminModifyPermissions(ITracer tracer, string directoryPath, out string error)
        {
            try
            {
                DirectorySecurity directorySecurity;
                if (Directory.Exists(directoryPath))
                {
                    directorySecurity = Directory.GetAccessControl(directoryPath);
                }
                else
                {
                    directorySecurity = new DirectorySecurity();
                }

                // Protect the access rules from inheritance and remove any inherited rules
                directorySecurity.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);

                // Remove any existing ACLs and add new ACLs for users and admins
                RemoveAllFileSystemAccessRulesFromDirectorySecurity(directorySecurity);
                AddUsersAccessRulesToDirectorySecurity(directorySecurity, grantUsersModifyPermissions: false);
                AddAdminAccessRulesToDirectorySecurity(directorySecurity);

                Directory.CreateDirectory(directoryPath, directorySecurity);

                // Ensure the ACLs are set correctly if the directory already existed
                Directory.SetAccessControl(directoryPath, directorySecurity);
            }
            catch (Exception e) when (e is IOException || e is SystemException)
            {
                EventMetadata metadata = new EventMetadata();
                metadata.Add("Exception", e.ToString());
                tracer.RelatedError(metadata, $"{nameof(this.TryCreateOrUpdateDirectoryToAdminModifyPermissions)}: Exception while creating/configuring directory");

                error = e.Message;
                return false;
            }

            error = null;
            return true;
        }

        public bool IsFileSystemSupported(string path, out string error)
        {
            error = null;
            return true;
        }

        private class NativeFileReader
        {
            private const uint GenericRead = 0x80000000;
            private const uint OpenExisting = 3;

            public static bool TryReadFirstByteOfFile(string fileName, byte[] buffer)
            {
                using (SafeFileHandle handle = Open(fileName))
                {
                    if (!handle.IsInvalid)
                    {
                        return ReadOneByte(handle, buffer);
                    }
                }

                return false;
            }

            private static SafeFileHandle Open(string fileName)
            {
                return CreateFile(fileName, GenericRead, (uint)(FileShare.ReadWrite | FileShare.Delete), 0, OpenExisting, 0, 0);
            }

            private static bool ReadOneByte(SafeFileHandle handle, byte[] buffer)
            {
                int bytesRead = 0;
                return ReadFile(handle, buffer, 1, ref bytesRead, 0);
            }

            [DllImport("kernel32", SetLastError = true, ThrowOnUnmappableChar = true, CharSet = CharSet.Unicode)]
            private static extern SafeFileHandle CreateFile(
                string fileName,
                uint desiredAccess,
                uint shareMode,
                uint securityAttributes,
                uint creationDisposition,
                uint flagsAndAttributes,
                int hemplateFile);

            [DllImport("kernel32", SetLastError = true)]
            private static extern bool ReadFile(
                SafeFileHandle file,
                [Out] byte[] buffer,
                int numberOfBytesToRead,
                ref int numberOfBytesRead,
                int overlapped);
        }
    }
}
