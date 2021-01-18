using System;
using System.Runtime.InteropServices;

using UnityEngine;
using UnityEditor;

[InitializeOnLoadAttribute]
public static class UnloadNativePlugin
{
    [DllImport("kernel32")]
    public static extern IntPtr GetModuleHandle(string moduleName);

    [DllImport("kernel32")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr handle);

    const string VLCPlugin = "VLCUnityPlugin";

    static UnloadNativePlugin()
    {
        EditorApplication.playModeStateChanged -= Unload;
        EditorApplication.playModeStateChanged += Unload;
    }

    private static void Unload(PlayModeStateChange state)
    {
        if(state != PlayModeStateChange.ExitingPlayMode) return;

        var module = GetModuleHandle(VLCPlugin);
        if(module == IntPtr.Zero) return;
        var result = FreeLibrary(module);
    }
}