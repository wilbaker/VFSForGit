using GVFS.Common.Tracing;
using System;
using System.Collections.Generic;
using System.IO;

namespace GVFS.Common
{
    public class ProductUpgraderInfo
    {
        public const string UpgradeDirectoryName = "GVFS.Upgrade";
        public const string LogDirectory = "UpgraderLogs";
        public const string DownloadDirectory = "Downloads";

        protected const string RootDirectory = UpgradeDirectoryName;

        private static readonly HashSet<string> GVFSInstallerFileNamePrefixCandidates = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "SetupGVFS",
            "VFSForGit"
        };

        public static bool IsLocalUpgradeAvailable(string installerExtension)
        {
            string downloadDirectory = GetAssetDownloadsPath();
            if (Directory.Exists(downloadDirectory))
            {
                foreach (string file in Directory.EnumerateFiles(downloadDirectory, "*", SearchOption.TopDirectoryOnly))
                {
                    string[] components = Path.GetFileName(file).Split('.');
                    int length = components.Length;
                    if (length >= 2 &&
                        GVFSInstallerFileNamePrefixCandidates.Contains(components[0]) &&
                        installerExtension.Equals(components[length - 1], StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        public static IEnumerable<string> PossibleGVFSInstallerNamePrefixes()
        {
            return GVFSInstallerFileNamePrefixCandidates;
        }

        public static string GetUpgradesDirectoryPath()
        {
            return Paths.GetServiceDataRoot(RootDirectory);
        }

        public static string GetLogDirectoryPath()
        {
            return Path.Combine(Paths.GetServiceDataRoot(RootDirectory), LogDirectory);
        }

        public static string GetAssetDownloadsPath()
        {
            return Path.Combine(
                Paths.GetServiceDataRoot(RootDirectory),
                DownloadDirectory);
        }

        /// <summary>
        /// Deletes any previously downloaded installers in the Upgrader Download directory.
        /// This can include old installers which were downloaded, but user never installed
        /// using gvfs upgrade and GVFS is now up to date already.
        /// </summary>
        public static void DeleteAllInstallerDownloads(ITracer tracer = null)
        {
            try
            {
                RecursiveDelete(ProductUpgraderInfo.GetAssetDownloadsPath());
            }
            catch (Exception ex)
            {
                if (tracer != null)
                {
                    tracer.RelatedError($"{nameof(DeleteAllInstallerDownloads)}: Could not remove directory: {ProductUpgraderInfo.GetAssetDownloadsPath()}.{ex.ToString()}");
                }
            }
        }

        private static void RecursiveDelete(string path)
        {
            if (!Directory.Exists(path))
            {
                return;
            }

            DirectoryInfo directory = new DirectoryInfo(path);

            foreach (FileInfo file in directory.GetFiles())
            {
                file.Attributes = FileAttributes.Normal;
                file.Delete();
            }

            foreach (DirectoryInfo subDirectory in directory.GetDirectories())
            {
                RecursiveDelete(subDirectory.FullName);
            }

            directory.Delete();
        }
    }
}
