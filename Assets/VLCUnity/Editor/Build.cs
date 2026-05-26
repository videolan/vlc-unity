using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

// standalone build step for copying necessary additional libvlc build files
public class CopyLibVLCFiles : IPostprocessBuildWithReport
{
    const string lua = "lua";
    const string hrtfs = "hrtfs";
    const string locale = "locale";
    const string VLCUnity = "VLCUnity";
    const string x64 = "x86_64";
    const string Plugins = "Plugins";
    const string Data = "_Data";
    const string standaloneWindows = "StandaloneWindows64";
    const string standaloneLinux = "StandaloneLinux64";
    const string Windows = "Windows";
    const string Linux = "Linux";
    const string vlc = "vlc";
    public int callbackOrder => 0;
    
    public void OnPostprocessBuild(BuildReport report)
    {
        if(report.summary.platform.ToString() == standaloneWindows)
        {
            PostprocessWindows(report);
        }
        else if(report.summary.platform.ToString() == standaloneLinux)
        {
            PostprocessLinux(report);
        }
    }

    void PostprocessWindows(BuildReport report)
    {
        var buildOutput = Path.GetDirectoryName(report.summary.outputPath);
        var libvlcBuildOutput = Path.Combine(buildOutput, $"{Application.productName}{Data}", Plugins, x64);
        var sourceLibvlcLocation = Path.Combine(Path.GetFullPath(Application.dataPath), VLCUnity, Plugins, Windows, x64);

        CopyFolder(Path.Combine(sourceLibvlcLocation, lua), Path.Combine(libvlcBuildOutput, lua));
        CopyFolder(Path.Combine(sourceLibvlcLocation, hrtfs), Path.Combine(libvlcBuildOutput, hrtfs));
        CopyFolder(Path.Combine(sourceLibvlcLocation, locale), Path.Combine(libvlcBuildOutput, locale));

        var cacheGenSource = Path.Combine(sourceLibvlcLocation, "vlc-cache-gen.exe");
        if (!File.Exists(cacheGenSource))
        {
            UnityEngine.Debug.LogWarning("vlc-cache-gen.exe not found, skipping plugin cache generation.");
            return;
        }
        var cacheGenExe = Path.Combine(libvlcBuildOutput, "vlc-cache-gen.exe");
        CopyFile(cacheGenSource, cacheGenExe);
        RunCacheGen(cacheGenExe, libvlcBuildOutput);
        File.Delete(cacheGenExe);
    }

    void PostprocessLinux(BuildReport report)
    {
        var buildOutput = Path.GetDirectoryName(report.summary.outputPath);
        var exeName = Path.GetFileNameWithoutExtension(report.summary.outputPath);
        var libvlcBuildOutput = Path.Combine(buildOutput, $"{exeName}{Data}", Plugins);
        var sourceLibvlcLocation = Path.Combine(Path.GetFullPath(Application.dataPath), VLCUnity, Plugins, Linux, x64);

        // Copy the vlc/ directory tree (plugins loaded by libvlc at runtime)
        CopyFolder(Path.Combine(sourceLibvlcLocation, vlc), Path.Combine(libvlcBuildOutput, vlc));

        // libVLCUnityPlugin.so has NEEDED entries for the SONAMEs
        // (libvlc.so.12, libvlccore.so.9). Unity only copies the unversioned
        // .so files into the build output, so make them resolvable under
        // their SONAME names here so the dynamic linker resolves them at runtime.
        LinkAsSoname(libvlcBuildOutput, "libvlc.so", "libvlc.so.12");
        LinkAsSoname(libvlcBuildOutput, "libvlccore.so", "libvlccore.so.9");
    }

    [DllImport("libc", EntryPoint = "symlink", SetLastError = true)]
    static extern int symlink(string target, string linkpath);

    static void LinkAsSoname(string directory, string source, string sonameName)
    {
        var srcPath = Path.Combine(directory, source);
        var dstPath = Path.Combine(directory, sonameName);
        if (!File.Exists(srcPath) || File.Exists(dstPath))
            return;
        if (symlink(source, dstPath) != 0)
            File.Copy(srcPath, dstPath);
    }

    void CopyFolder(string sourceFolder, string destFolder)
    {
        if(!Directory.Exists(sourceFolder))
            return;
        if (!Directory.Exists( destFolder ))
            Directory.CreateDirectory( destFolder );
        string[] files = Directory.GetFiles( sourceFolder );
        foreach (string file in files)
        {
            string name = Path.GetFileName( file );
            string dest = Path.Combine( destFolder, name );
            CopyFile( file, dest );
        }
        string[] folders = Directory.GetDirectories( sourceFolder );
        foreach (string folder in folders)
        {
            string name = Path.GetFileName( folder );
            string dest = Path.Combine( destFolder, name );
            CopyFolder( folder, dest );
        }
    }

    void CopyFile(string sourceFile, string destFile, bool overwrite = true)
    {
        if(File.Exists(sourceFile))
        {
            File.Copy(sourceFile, destFile, overwrite);
        }
    }

    static void RunCacheGen(string exePath, string pluginsPath)
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = exePath,
                Arguments = $"\"{pluginsPath}\"",
                WorkingDirectory = pluginsPath,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            using var process = Process.Start(psi);
            process.WaitForExit();
            if (process.ExitCode != 0)
            {
                var stderr = process.StandardError.ReadToEnd();
                UnityEngine.Debug.LogWarning($"vlc-cache-gen exited with code {process.ExitCode}: {stderr}");
            }
        }
        catch (System.Exception e)
        {
            UnityEngine.Debug.LogWarning($"Failed to run vlc-cache-gen: {e.Message}");
        }
    }
}