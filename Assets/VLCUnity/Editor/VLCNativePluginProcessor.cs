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
        const string IOS_PATH = "VLCUnity/Plugins/iOS/";

#if UNITY_SUPPORTS_BUILD_REPORT
        public void OnPreprocessBuild(BuildReport report)
        {
#if UNITY_IPHONE
            PatchIOSFrameworkMinimumOSVersion();
#endif
        }

#if UNITY_IPHONE
        internal static void PatchIOSFrameworkMinimumOSVersion()
        {
            string targetVersion = PlayerSettings.iOS.targetOSVersionString;
            if (string.IsNullOrEmpty(targetVersion))
                return;

            string iosPluginsPath = Path.Combine(Application.dataPath, IOS_PATH);
            if (!Directory.Exists(iosPluginsPath))
                return;

            string[] plists = Directory.GetFiles(iosPluginsPath, "Info.plist", SearchOption.AllDirectories);
            int updated = 0;
            foreach (string plistPath in plists)
            {
                if (!plistPath.Replace("\\", "/").Contains(".framework/"))
                    continue;

                PlistDocument plist = new PlistDocument();
                plist.ReadFromFile(plistPath);

                PlistElementDict root = plist.root;
                if (root["MinimumOSVersion"] == null)
                    continue;

                string currentVersion = root["MinimumOSVersion"].AsString();
                if (currentVersion == targetVersion)
                    continue;

                root.SetString("MinimumOSVersion", targetVersion);
                plist.WriteToFile(plistPath);
                updated++;
            }

            if (updated > 0)
                Debug.Log($"[VLCUnity] Updated MinimumOSVersion to {targetVersion} in {updated} framework Info.plist files");
        }
#endif
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
            if(buildTarget != BuildTarget.StandaloneOSX)
                return;
            
            var platformName = BuildPipeline.GetBuildTargetName(buildTarget);
            // TODO: this is a hidden, no more documented Unity Editor API. Fail gracefully if not available
            var cpu = EditorUserBuildSettings.GetPlatformSettings(platformName, "Architecture");
            if(cpu == "x64ARM64") // Universal builds are not yet supported
            {
                Debug.LogError("Universal architecture not yet supported. Please select either Intel 64-bit or Apple Silicon ARM");
                return;
            }

            if(path.EndsWith(".app"))
            {
                // "Create XCode Project" is unchecked, no further processing required
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
                if(!pi.isNativePlugin || !pi.assetPath.Contains(IOS_PATH) || pi.assetPath.Contains("LoadPlugin.mm"))
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
        }
    }

    [InitializeOnLoad]
    class LinuxPluginPostprocessor : AssetPostprocessor
    {
        const string LINUX_PATH = "VLCUnity/Plugins/Linux/x86_64";
        const string VLC_PLUGINS_PATH = "VLCUnity/Plugins/Linux/x86_64/vlc/";
        static readonly Regex LinuxSonameRegex = new Regex(@"\.so\.\d+$");

        static LinuxPluginPostprocessor()
        {
            if (Application.platform == RuntimePlatform.LinuxEditor)
                EditorApplication.delayCall += FixExistingLinuxPlugins;
        }

        static void FixExistingLinuxPlugins()
        {
            var guids = AssetDatabase.FindAssets("", new[] { "Assets/VLCUnity/Plugins/Linux" });
            if (guids.Length == 0) return;

            bool anyDirty = false;
            foreach (var guid in guids)
            {
                string assetPath = AssetDatabase.GUIDToAssetPath(guid);
                PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                if (pi == null) continue;

                if (ShouldExcludeFromUnityPluginLoading(assetPath))
                {
                    if (pi.GetCompatibleWithAnyPlatform() || pi.GetCompatibleWithEditor()
                        || pi.GetCompatibleWithPlatform(BuildTarget.StandaloneLinux64))
                    {
                        anyDirty = true;
                        break;
                    }
                    continue;
                }

                if (pi.GetCompatibleWithAnyPlatform() || !pi.GetCompatibleWithEditor()
                    || !pi.GetCompatibleWithPlatform(BuildTarget.StandaloneLinux64))
                {
                    anyDirty = true;
                    break;
                }
            }

            if (!anyDirty) return;

            var paths = new string[guids.Length];
            for (int i = 0; i < guids.Length; i++)
                paths[i] = AssetDatabase.GUIDToAssetPath(guids[i]);

            OnPostprocessAllAssets(paths, new string[0], new string[0], new string[0]);
        }

        static void OnPostprocessAllAssets(string[] importedAssets, string[] _, string[] __, string[] ___)
        {
            try
            {
                AssetDatabase.StartAssetEditing();
                foreach (string assetPath in importedAssets)
                {
                    if (!assetPath.Contains(LINUX_PATH))
                    {
                        continue;
                    }

                    PluginImporter pi = AssetImporter.GetAtPath(assetPath) as PluginImporter;
                    if (pi == null) continue;

                    // VLC runtime plugins (vlc/plugins/*) are loaded by libvlc,
                    // not by Unity. Exclude them from the build and the Editor.
                    // Also exclude Linux runtime payloads under vlc/ and top-level
                    // SONAME aliases (e.g. libvlc.so.12). Querying isNativePlugin on
                    // those versioned assets triggers a Unity assertion on Linux.
                    if (ShouldExcludeFromUnityPluginLoading(assetPath))
                    {
                        if (pi.GetCompatibleWithAnyPlatform() || pi.GetCompatibleWithEditor()
                            || pi.GetCompatibleWithPlatform(BuildTarget.StandaloneLinux64))
                        {
                            pi.SetCompatibleWithAnyPlatform(false);
                            pi.SetCompatibleWithEditor(false);
                            pi.SetCompatibleWithPlatform(BuildTarget.StandaloneLinux64, false);
                            pi.SaveAndReimport();
                        }
                        continue;
                    }

                    var dirty = false;

                    if (pi.GetCompatibleWithAnyPlatform() || !pi.GetCompatibleWithEditor() || !pi.GetCompatibleWithPlatform(BuildTarget.StandaloneLinux64))
                    {
                        pi.SetCompatibleWithAnyPlatform(false);
                        pi.SetCompatibleWithEditor(true);
                        pi.SetCompatibleWithPlatform(BuildTarget.StandaloneLinux64, true);

                        dirty = true;
                    }

                    var cpu = pi.GetPlatformData(BuildTarget.StandaloneLinux64, "CPU");
                    if (cpu != "x86_64")
                    {
                        pi.SetPlatformData(BuildTarget.StandaloneLinux64, "CPU", "x86_64");
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
        }

        static bool ShouldExcludeFromUnityPluginLoading(string assetPath)
        {
            return assetPath.Contains(VLC_PLUGINS_PATH) || LinuxSonameRegex.IsMatch(assetPath);
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
        }

    }

    class VLCUnityPluginPreprocessor : AssetPostprocessor
    {
        const string WINDOWS_PATH = "VLCUnity/Plugins/Windows/x86_64";
        const string UWP_PATH = "VLCUnity/Plugins/WSA/UWP";
        const string MACOS_PATH = "VLCUnity/Plugins/MacOS";

        void OnPreprocessAsset()
        {
            PluginImporter pi = assetImporter as PluginImporter;
            if (pi == null || !pi.isNativePlugin)
                return;

            if (assetPath.Contains(WINDOWS_PATH))
            {
                pi.SetCompatibleWithAnyPlatform(false);
                pi.SetCompatibleWithEditor(true);
                pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, true);
                pi.SetPlatformData(BuildTarget.StandaloneWindows64, "CPU", "x86_64");
            }
            else if (assetPath.Contains(UWP_PATH))
            {
                pi.SetCompatibleWithAnyPlatform(false);
                pi.SetCompatibleWithEditor(false);
                pi.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, false);
                pi.SetCompatibleWithPlatform(BuildTarget.WSAPlayer, true);

                var isX64 = assetPath.Contains($"{UWP_PATH}/x86_64");
                pi.SetPlatformData(BuildTarget.WSAPlayer, "CPU", isX64 ? "X64" : "ARM64");
            }
            else if (assetPath.Contains(MACOS_PATH))
            {
                bool isArm64Host = RuntimeInformation.ProcessArchitecture == Architecture.Arm64;
                pi.SetCompatibleWithAnyPlatform(false);
                pi.SetCompatibleWithPlatform(BuildTarget.StandaloneOSX, true);

                if (assetPath.Contains($"{MACOS_PATH}/ARM64/"))
                {
                    pi.SetCompatibleWithEditor(isArm64Host);
                    pi.SetPlatformData(BuildTarget.StandaloneOSX, "CPU", "ARM64");
                }
                else if (assetPath.Contains($"{MACOS_PATH}/x86_64/"))
                {
                    pi.SetCompatibleWithEditor(!isArm64Host);
                    pi.SetPlatformData(BuildTarget.StandaloneOSX, "CPU", "x86_64");
                }
            }
        }
    }

    [InitializeOnLoad]
    class LibVLCSharpPluginPostprocessor : AssetPostprocessor
    {
        const string LIBVLCSHARP_DLL = "LibVLCSharp.dll";
        const string IOS_PATH = "VLCUnity/Plugins/iOS/";

        static LibVLCSharpPluginPostprocessor()
        {
            EditorApplication.delayCall += () =>
            {
                foreach (var pi in PluginImporter.GetAllImporters())
                    if (pi != null && pi.assetPath.EndsWith(LIBVLCSHARP_DLL))
                        Configure(pi);
            };
        }

        static void OnPostprocessAllAssets(string[] imported, string[] _, string[] __, string[] ___)
        {
            foreach (var path in imported)
                if (path.EndsWith(LIBVLCSHARP_DLL) &&
                    AssetImporter.GetAtPath(path) is PluginImporter pi)
                    Configure(pi);
        }

        static void Configure(PluginImporter pi)
        {
            bool isIOS = pi.assetPath.Contains(IOS_PATH);
            bool changed = false;

            changed |= SetAny(pi, false);
            if (isIOS)
            {
                changed |= SetEditor(pi, false);
                changed |= SetPlatform(pi, BuildTarget.iOS, true);
            }
            else
            {
                changed |= SetEditor(pi, true);
                changed |= SetPlatform(pi, BuildTarget.iOS, false);
                changed |= SetPlatform(pi, BuildTarget.StandaloneOSX, true);
                changed |= SetPlatform(pi, BuildTarget.StandaloneWindows, true);
                changed |= SetPlatform(pi, BuildTarget.StandaloneWindows64, true);
                changed |= SetPlatform(pi, BuildTarget.StandaloneLinux64, true);
                changed |= SetPlatform(pi, BuildTarget.Android, true);
                changed |= SetPlatform(pi, BuildTarget.WSAPlayer, true);
                changed |= SetPlatform(pi, BuildTarget.XboxOne, true);
            }

            if (changed) pi.SaveAndReimport();
        }

        static bool SetAny(PluginImporter pi, bool v)
        { if (pi.GetCompatibleWithAnyPlatform() == v) return false; pi.SetCompatibleWithAnyPlatform(v); return true; }
        static bool SetEditor(PluginImporter pi, bool v)
        { if (pi.GetCompatibleWithEditor() == v) return false; pi.SetCompatibleWithEditor(v); return true; }
        static bool SetPlatform(PluginImporter pi, BuildTarget t, bool v)
        { if (pi.GetCompatibleWithPlatform(t) == v) return false; pi.SetCompatibleWithPlatform(t, v); return true; }
    }
}
