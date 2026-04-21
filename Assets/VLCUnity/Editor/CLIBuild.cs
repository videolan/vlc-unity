using UnityEditor;
using UnityEngine;

public class CLIBuild
{
    public static void BuildLinux()
    {
        var scenes = new string[EditorBuildSettings.scenes.Length];
        for (int i = 0; i < EditorBuildSettings.scenes.Length; i++)
            scenes[i] = EditorBuildSettings.scenes[i].path;

        if (scenes.Length == 0)
        {
            Debug.LogError("No scenes in build settings!");
            EditorApplication.Exit(1);
            return;
        }

        var report = BuildPipeline.BuildPlayer(scenes, "build/app.x86_64",
            BuildTarget.StandaloneLinux64, BuildOptions.None);

        if (report.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded)
        {
            Debug.LogError("Build failed: " + report.summary.result);
            EditorApplication.Exit(1);
        }
    }
}
