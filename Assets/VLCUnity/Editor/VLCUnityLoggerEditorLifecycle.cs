using UnityEditor;

namespace LibVLCSharp
{
    [InitializeOnLoad]
    internal static class VLCUnityLoggerEditorLifecycle
    {
        static VLCUnityLoggerEditorLifecycle()
        {
            AssemblyReloadEvents.beforeAssemblyReload -= Shutdown;
            AssemblyReloadEvents.beforeAssemblyReload += Shutdown;

            EditorApplication.playModeStateChanged -= OnPlayModeStateChanged;
            EditorApplication.playModeStateChanged += OnPlayModeStateChanged;

            EditorApplication.quitting -= Shutdown;
            EditorApplication.quitting += Shutdown;
        }

        private static void OnPlayModeStateChanged(PlayModeStateChange state)
        {
            if (state == PlayModeStateChange.ExitingPlayMode)
                Shutdown();
        }

        private static void Shutdown()
        {
            VLCUnityLogger.OnQuit();
        }
    }
}
