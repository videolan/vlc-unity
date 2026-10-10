using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Videolabs.VLCUnity.Packaging
{
    // Exports Assets/VLCUnity as a .unitypackage that imports correctly in Unity 2022.3 and
    // newer, whichever editor runs the export. This folder is outside Assets/VLCUnity, so the
    // packaging tooling itself is never shipped to customers.
    //
    // Command line:
    //   Unity -batchmode -quit -projectPath <repo> -executeMethod Videolabs.VLCUnity.Packaging.PackageExporter.ExportFromCommandLine -exportPath <out.unitypackage>
    public static class PackageExporter
    {
        const string ExportRoot = "Assets/VLCUnity";
        const string MenuRoot = "Tools/VLC Unity/Packaging/";

        [MenuItem(MenuRoot + "Export .unitypackage...", false, 200)]
        static void ExportMenu()
        {
            string output = EditorUtility.SaveFilePanel("Export VLC Unity package", "", "vlc-unity.unitypackage", "unitypackage");
            if (string.IsNullOrEmpty(output))
                return;
            RunWithProgress("Exporting VLC Unity package", () => Export(output), output);
        }

        public static UnityPackageReport Export(string outputPath)
        {
            string exported = Path.GetFullPath(FileUtil.GetUniqueTempPathInProject() + ".unitypackage");
            try
            {
                AssetDatabase.ExportPackage(ExportRoot, exported, ExportPackageOptions.Recurse);
                if (!File.Exists(exported))
                    throw new IOException($"Unity did not export {ExportRoot}.");
                return UnityPackageRewriter.MakeCompatible(exported, outputPath);
            }
            finally
            {
                if (File.Exists(exported))
                    File.Delete(exported);
            }
        }

        public static void ExportFromCommandLine()
        {
            try
            {
                string output = Path.GetFullPath(RequireArgument("-exportPath"));
                Log(Export(output), output);
            }
            catch (Exception e)
            {
                Debug.LogException(e);
                EditorApplication.Exit(1);
            }
        }

        static void RunWithProgress(string title, Func<UnityPackageReport> run, string output)
        {
            try
            {
                EditorUtility.DisplayProgressBar(title, output, 0.5f);
                Log(run(), output);
                EditorUtility.RevealInFinder(output);
            }
            catch (Exception e)
            {
                Debug.LogException(e);
                EditorUtility.DisplayDialog(title, e.Message, "OK");
            }
            finally
            {
                EditorUtility.ClearProgressBar();
            }
        }

        static void Log(UnityPackageReport report, string output)
        {
            Debug.Log($"[VLC Unity packaging] {output}: rewrote {report.Converted.Count} plugin metas to serializedVersion 2, " +
                $"{report.AlreadyCompatible} already compatible.");
        }

        static string RequireArgument(string name)
        {
            string[] args = Environment.GetCommandLineArgs();
            int index = Array.IndexOf(args, name);
            if (index < 0 || index + 1 >= args.Length)
                throw new ArgumentException($"Missing {name} <path> argument.");
            return args[index + 1];
        }
    }
}
