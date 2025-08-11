#if UNITY_2018_1_OR_NEWER
#define UNITY_SUPPORTS_BUILD_REPORT
#endif
using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Linq;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEditor;
using UnityEngine.Rendering;
using UnityEditor.Build;
using UnityEditor.Callbacks;

#if UNITY_SUPPORTS_BUILD_REPORT
using UnityEditor.Build.Reporting;
#endif

#if UNITY_IPHONE
using UnityEditor.iOS.Xcode;
using UnityEditor.iOS.Xcode.Extensions;
#endif

namespace Videolabs.VLCUnity.Editor
{
    public class VLCNativePluginProcessor :
#if UNITY_SUPPORTS_BUILD_REPORT
        IPreprocessBuildWithReport
#else
        IPreprocessBuild
#endif
    {
        public int callbackOrder { get { return 0; } }

#if UNITY_SUPPORTS_BUILD_REPORT
        public void OnPreprocessBuild(BuildReport report)
        {
        }
#endif
        internal static void CopyAndReplaceDirectory(string srcPath, string dstPath)
        {
            if (Directory.Exists(dstPath))
                Directory.Delete(dstPath);
            if (File.Exists(dstPath))
                File.Delete(dstPath);

            Directory.CreateDirectory(dstPath);

            foreach (var file in Directory.GetFiles(srcPath))
                File.Copy(file, Path.Combine(dstPath, Path.GetFileName(file)));

            foreach (var dir in Directory.GetDirectories(srcPath))
                CopyAndReplaceDirectory(dir, Path.Combine(dstPath, Path.GetFileName(dir)));
        }

        [PostProcessBuildAttribute(1)]
        public static void OnPostprocessBuild(BuildTarget buildTarget, string path)
        {
            if (buildTarget == BuildTarget.StandaloneOSX || buildTarget == BuildTarget.iOS)
            {
                OnPostprocessBuildMac(buildTarget, path);
#if UNITY_IPHONE
                OnPostprocessBuildiPhone(path);
#endif
            }
        }

        internal static void OnPostprocessBuildMac(BuildTarget buildTarget, string path)
        {
            if(path.EndsWith(".app") || buildTarget != BuildTarget.StandaloneOSX)
            {
                // "Create XCode Project" is unchecked
                return;
            }

            // XCode patching
            var projectPath = Path.Combine(path, Path.GetFileName(path) + ".xcodeproj", "project.pbxproj");

            string originalPbxprojContent = File.ReadAllText(projectPath);

            Regex regex = new Regex(@"VALID_ARCHS\s*=\s*([^;]+)");
            Match match = regex.Match(originalPbxprojContent);

            bool appleSiliconBuild = false;

            if (match.Success)
            {
                string arch = match.Groups[1].Value;
                if(arch == "arm64")
                {
                    appleSiliconBuild = true;
                }
                else if(arch == "x86_64")
                {
                    appleSiliconBuild = false;
                }
                else // likely "arm64 x86_64"
                {
                    Debug.LogError("Universal macOS binary is not yet supported, reach out on our gitlab for updates");
                }
            }
            else
            {
                Debug.LogError("No CPU target found while parsing the XCode project file.");
            }

            string pattern = @"(path\s*=\s*""[^""]*/Plugins/)(ARM64/|x86_64/)?([^""]+\.dylib)"";";
            string replacement;
            if (appleSiliconBuild)
            {
                replacement = @"$1ARM64/$3"";";
            }
            else
            {
                replacement = @"$1x86_64/$3"";";
            }
            string modifiedContent = Regex.Replace(originalPbxprojContent, pattern, replacement);
            File.WriteAllText(projectPath, modifiedContent);
        }

#if UNITY_IPHONE
        internal static void AddIOSPlugin(PBXProject proj, string target, string plugin)
        {
          string fileName = Path.GetFullPath(plugin);
          string fileRef = proj.AddFile(fileName, plugin, PBXSourceTree.Source);
          proj.AddFileToEmbedFrameworks(target, fileRef);
        }

        internal static void OnPostprocessBuildiPhone(string path)
        {
            string projPath = PBXProject.GetPBXProjectPath(path);
            PBXProject proj = new PBXProject();

            proj.ReadFromString(File.ReadAllText(projPath));
            #if UNITY_2020_2_OR_NEWER
            string target = proj.GetUnityMainTargetGuid();
            #else
            string target = proj.TargetGuidByName("Unity-iPhone");
            #endif
            proj.SetBuildProperty(target, "ENABLE_BITCODE", "NO");

            string frameworkTarget = proj.GetUnityFrameworkTargetGuid();
            proj.SetBuildProperty(frameworkTarget, "ENABLE_BITCODE", "NO");

            PluginImporter[] importers = PluginImporter.GetAllImporters();
            foreach (PluginImporter pi in importers)
            {
                if(!pi.isNativePlugin || !pi.assetPath.Contains(IOS_PATH) || pi.assetPath.Contains(IOS_LOADPLUGIN_SOURCE))
                {
                    continue;
                }

                var isX64 = pi.assetPath.Contains("iOS/x86_64");

                // XCode patching
                if((PlayerSettings.iOS.sdkVersion == iOSSdkVersion.DeviceSDK && !isX64)
                    || (PlayerSettings.iOS.sdkVersion == iOSSdkVersion.SimulatorSDK && isX64))
                {
                    AddIOSPlugin(proj, target, pi.assetPath);
                }
            }
            File.WriteAllText(projPath, proj.WriteToString());
        }
#endif
    }

    class iOSPluginPostprocessor : AssetPostprocessor
    {
        const string IOS_PATH = "VLCUnity/Plugins/iOS/";

        static void OnPostprocessAllAssets(string[] importedAssets, string[] _, string[] __, string[] ___)
        {
            try
            {
                AssetDatabase.StartAssetEditing();
                foreach (string assetPath in importedAssets)
                {
                    if (!assetPath.Contains(IOS_PATH))
                    {
                        continue;
                    }

                    PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                    if (pi == null || !pi.isNativePlugin) continue;

                    var isX64 = pi.assetPath.Contains("iOS/x86_64");

                    var dirty = false;
                    
                    if (pi.GetCompatibleWithAnyPlatform() || pi.GetCompatibleWithEditor() || !pi.GetCompatibleWithPlatform(BuildTarget.iOS))
                    {
                        pi.SetCompatibleWithAnyPlatform(false);
                        pi.SetCompatibleWithEditor(false);
                        pi.SetCompatibleWithPlatform(BuildTarget.iOS, true);
                        dirty = true;
                    }

                    var cpu = pi.GetPlatformData(BuildTarget.iOS, "CPU");

                    if (isX64)
                    {
                        if (cpu != "X64")
                        {
                            pi.SetPlatformData(BuildTarget.iOS, "CPU", "X64");
                            dirty = true;
                        }
                    }
                    else
                    {
                        if (cpu != "ARM64")
                        {
                            pi.SetPlatformData(BuildTarget.iOS, "CPU", "ARM64");
                            dirty = true;
                        }
                    }

                    if (dirty)
                    {
                        pi.SaveAndReimport();
                    }
                }
            }
            finally
            {
                AssetDatabase.StopAssetEditing();
                AssetDatabase.SaveAssets();
                AssetDatabase.Refresh();
            }
        }
    }

    class AndroidPluginPostprocessor : AssetPostprocessor
    {
        const string ANDROID_PATH = "VLCUnity/Plugins/Android/libs";
        static void OnPostprocessAllAssets(string[] importedAssets, string[] _, string[] __, string[] ___)
        {
            try
            {
                AssetDatabase.StartAssetEditing();
                foreach (string assetPath in importedAssets)
                {
                    if (!assetPath.Contains(ANDROID_PATH))
                    {
                        continue;
                    }

                    PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                    if (pi == null || !pi.isNativePlugin) continue;

                    var dirty = false;
                    
                    if (pi.GetCompatibleWithAnyPlatform() || pi.GetCompatibleWithEditor() || !pi.GetCompatibleWithPlatform(BuildTarget.Android))
                    {
                        pi.SetCompatibleWithAnyPlatform(false);
                        pi.SetCompatibleWithEditor(false);
                        pi.SetCompatibleWithPlatform(BuildTarget.Android, true);
                        dirty = true;
                    }

                    if (pi.assetPath.Contains($"{ANDROID_PATH}/armeabi-v7a/"))
                    {
                        if (pi.GetPlatformData(BuildTarget.Android, "CPU") != "ARMv7")
                        {
                            pi.SetPlatformData(BuildTarget.Android, "CPU", "ARMv7");
                            dirty = true;
                        }
                    }
                    else if (pi.assetPath.Contains($"{ANDROID_PATH}/arm64-v8a/"))
                    {
                        if (pi.GetPlatformData(BuildTarget.Android, "CPU") != "ARM64")
                        {
                            pi.SetPlatformData(BuildTarget.Android, "CPU", "ARM64");
                            dirty = true;
                        }
                    }
                    else if (pi.assetPath.Contains($"{ANDROID_PATH}/x86/"))
                    {
                        if (pi.GetPlatformData(BuildTarget.Android, "CPU") != "X86")
                        {
                            pi.SetPlatformData(BuildTarget.Android, "CPU", "X86");
                            dirty = true;
                        }
                    }
                    else if (pi.assetPath.Contains($"{ANDROID_PATH}/x86_64/"))
                    {
                        if (pi.GetPlatformData(BuildTarget.Android, "CPU") != "X86_64")
                        {
                            pi.SetPlatformData(BuildTarget.Android, "CPU", "X86_64");
                            dirty = true;
                        }
                    }

                    if (dirty)
                    {
                        pi.SaveAndReimport();
                    }

                }
            }
            finally
            {
                AssetDatabase.StopAssetEditing();
                AssetDatabase.SaveAssets();
                AssetDatabase.Refresh();
            }
        }
    }

    static class PluginErrorCleaner
    {
        internal static void ClearPluginErrors()
        {
            try
            {
                var logEntries = System.Type.GetType("UnityEditor.LogEntries, UnityEditor");
                var clearMethod = logEntries?.GetMethod("Clear", System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public);
                clearMethod?.Invoke(null, null);
            }
            catch
            {
            }
        }
    }

    class MacOSPluginPostprocessor : AssetPostprocessor
    {
        private const string MACOS_PATH = "VLCUnity/Plugins/MacOS";

        static void OnPostprocessAllAssets(string[] importedAssets, string[] _, string[] __, string[] ___)
        {
            bool isArm64Host = RuntimeInformation.ProcessArchitecture == Architecture.Arm64;

            try
            {
                AssetDatabase.StartAssetEditing();
                foreach (string assetPath in importedAssets)
                {
                    if (!assetPath.Contains(MACOS_PATH))
                    {
                        continue;
                    }

                    PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                    if (pi == null || !pi.isNativePlugin) continue;

                    var dirty = false;
                    
                    // Ensure the plugin is macOS-only
                    if (pi.GetCompatibleWithAnyPlatform() || !pi.GetCompatibleWithPlatform(BuildTarget.StandaloneOSX))
                    {
                        pi.SetCompatibleWithAnyPlatform(false);
                        pi.SetCompatibleWithPlatform(BuildTarget.StandaloneOSX, true);
                        dirty = true;
                    }
                    // AnyCPU / macOS universal binary is not yet supported.
                    var isEditorCompatible = pi.GetCompatibleWithEditor();
                    if(pi.assetPath.Contains($"{MACOS_PATH}/ARM64/"))
                    {
                        if(isArm64Host)
                        {
                            if(!isEditorCompatible)
                            {
                                pi.SetCompatibleWithEditor(true);
                                dirty = true;
                            }
                        }
                        else
                        {
                            if(isEditorCompatible)
                            {
                                pi.SetCompatibleWithEditor(false);
                                dirty = true;
                            }
                        }
                        if(pi.GetPlatformData(BuildTarget.StandaloneOSX, "CPU") != "ARM64")
                        {
                            pi.SetPlatformData(BuildTarget.StandaloneOSX, "CPU", "ARM64");
                            dirty = true;
                        }
                    }
                    else if(pi.assetPath.Contains($"{MACOS_PATH}/x86_64/"))
                    {
                        if(!isArm64Host)
                        {
                            if(!isEditorCompatible)
                            {
                                pi.SetCompatibleWithEditor(true);
                                dirty = true;
                            }
                        }
                        else
                        {
                            if(isEditorCompatible)
                            {
                                pi.SetCompatibleWithEditor(false);
                                dirty = true;
                            }
                        }
                        if(pi.GetPlatformData(BuildTarget.StandaloneOSX, "CPU") != "x86_64")
                        {
                            pi.SetPlatformData(BuildTarget.StandaloneOSX, "CPU", "x86_64");
                            dirty = true;
                        }
                    }

                    if (dirty)
                    {
                        pi.SaveAndReimport();
                    }
                }
            }
            finally
            {
                AssetDatabase.StopAssetEditing();
                AssetDatabase.SaveAssets();
                AssetDatabase.Refresh();
            }
            // the macOS editor is wrongly showing errors about duplicate architecture libvlc binaries being both Editor-compatible. Its confused, builds just fine and clears errors automatically after. We clear these errors immediately as to not confuse the user
            PluginErrorCleaner.ClearPluginErrors();
        }
    }

    class WindowsPluginPostprocessor : AssetPostprocessor
    {
        const string WINDOWS_PATH = "VLCUnity/Plugins/Windows/x86_64";
        static void OnPostprocessAllAssets(string[] importedAssets, string[] _, string[] __, string[] ___)
        {
            try
            {
                AssetDatabase.StartAssetEditing();
                foreach (string assetPath in importedAssets)
                {
                    if (!assetPath.Contains(WINDOWS_PATH))
                    {
                        continue;
                    }

                    PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                    if (pi == null || !pi.isNativePlugin) continue;

                    var dirty = false;

                    if (pi.GetCompatibleWithAnyPlatform() || !pi.GetCompatibleWithEditor() || !pi.GetCompatibleWithPlatform(BuildTarget.StandaloneWindows64))
                    {
                        pi.SetCompatibleWithAnyPlatform(false);
                        pi.SetCompatibleWithEditor(true);
                        pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, true);

                        dirty = true;
                    }

                    var cpu = pi.GetPlatformData(BuildTarget.StandaloneWindows64, "CPU");
                    if (cpu != "x86_64")
                    {
                        pi.SetPlatformData(BuildTarget.StandaloneWindows64, "CPU", "x86_64");
                        dirty = true;
                    }

                    if (dirty)
                    {
                        pi.SaveAndReimport();
                    }
                }
            }
            finally
            {
                AssetDatabase.StopAssetEditing();
                AssetDatabase.SaveAssets();
                AssetDatabase.Refresh();
            }
            // Clear false positive plugin errors about Windows plugin conflicts
            PluginErrorCleaner.ClearPluginErrors();
        }
    }

    class UWPPluginPostprocessor : AssetPostprocessor
    {
        static string[] UWP_ARCH = { "x86_64", "ARM64" };

        const string UWP_PATH = "VLCUnity/Plugins/WSA/UWP";
        static void OnPostprocessAllAssets(string[] importedAssets, string[] _, string[] __, string[] ___)
        {
            try
            {
                AssetDatabase.StartAssetEditing();
                foreach (string assetPath in importedAssets)
                {
                    if (!assetPath.Contains(UWP_PATH))
                    {
                        continue;
                    }

                    PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                    if (pi == null || !pi.isNativePlugin) continue;

                    var isX64 = pi.assetPath.Contains($"{UWP_PATH}/{UWP_ARCH[0]}");

                    // pi.ClearSettings();
                    var dirty = false;
                    if (pi.GetCompatibleWithAnyPlatform() || pi.GetCompatibleWithEditor() || !pi.GetCompatibleWithPlatform(BuildTarget.WSAPlayer) || pi.GetCompatibleWithPlatform(BuildTarget.StandaloneWindows64))
                    {
                        pi.SetCompatibleWithAnyPlatform(false);
                        pi.SetCompatibleWithEditor(false);
                        pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, false);

                        pi.SetCompatibleWithPlatform(BuildTarget.WSAPlayer, true);

                        dirty = true;
                    }

                    var cpu = pi.GetPlatformData(BuildTarget.WSAPlayer, "CPU");

                    if (isX64)
                    {
                        if (cpu != "X64")
                        {
                            pi.SetPlatformData(BuildTarget.WSAPlayer, "CPU", "X64");
                            dirty = true;
                        }
                    }
                    else
                    {
                        if (cpu != "ARM64")
                        {
                            pi.SetPlatformData(BuildTarget.WSAPlayer, "CPU", "ARM64");
                            dirty = true;
                        }
                    }

                    if (dirty)
                    {
                        pi.SaveAndReimport();
                    }
                }
            }
            finally
            {
                AssetDatabase.StopAssetEditing();
                AssetDatabase.SaveAssets();
                AssetDatabase.Refresh();
            }
            // Clear false positive plugin errors about Windows plugin conflicts
            PluginErrorCleaner.ClearPluginErrors();
        }
    }

    class VLCUnityPluginPreprocessor : AssetPostprocessor
    {
        void OnPreprocessAsset()
        {
            // Only process VLCUnityPlugin.dll files to workaround incorrect Unity Editor error
            if (!assetPath.EndsWith("VLCUnityPlugin.dll"))
                return;

            PluginImporter pi = assetImporter as PluginImporter;
            if (pi == null || !pi.isNativePlugin) 
                return;

            if (assetPath.Contains("VLCUnity/Plugins/Windows/x86_64"))
            {
                pi.SetCompatibleWithAnyPlatform(false);
                pi.SetCompatibleWithEditor(true);
                pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, true);
            }
            else if (assetPath.Contains("VLCUnity/Plugins/WSA/UWP"))
            {
                pi.SetCompatibleWithAnyPlatform(false);
                pi.SetCompatibleWithEditor(false);
                pi.SetCompatibleWithPlatform(BuildTarget.WSAPlayer, true);
                
                var isX64 = assetPath.Contains("VLCUnity/Plugins/WSA/UWP/x86_64");
                pi.SetPlatformData(BuildTarget.WSAPlayer, "CPU", isX64 ? "X64" : "ARM64");
            }
        }
    }

    class LibVLCSharpPluginPreprocessor : AssetPostprocessor
    {
        const string IOS_PATH = "VLCUnity/Plugins/iOS/";
        void OnPreprocessAsset()
        {
            var libvlcsharpDlls = PluginImporter.GetAllImporters().Where(pi => pi.assetPath.EndsWith("LibVLCSharp.dll")).ToList();

            foreach (var pi in libvlcsharpDlls)
            {
                if (pi.assetPath.Contains(IOS_PATH))
                {
                    pi.SetCompatibleWithAnyPlatform(false);
                    pi.SetCompatibleWithEditor(false);
                    pi.SetCompatibleWithPlatform(BuildTarget.iOS, true);
                }
                else
                {
                    pi.SetCompatibleWithPlatform(BuildTarget.StandaloneOSX, true);
                    pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows, true);
                    pi.SetCompatibleWithPlatform(BuildTarget.Android, true);
                    pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, true);
                    pi.SetCompatibleWithPlatform(BuildTarget.WSAPlayer, true);
                    pi.SetCompatibleWithPlatform(BuildTarget.XboxOne, true);

                    pi.SetCompatibleWithPlatform(BuildTarget.iOS, false);

                    pi.SetCompatibleWithAnyPlatform(false);
                    pi.SetCompatibleWithEditor(true);
                }
            }
        }
    }
}