using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace LibVLCSharp
{
    class OnLoad
    {
        static bool _rendererCleanupPending;
        static int _rendererCleanupNotBeforeFrame = -1;

#if !UNITY_EDITOR_WIN && (UNITY_ANDROID || UNITY_STANDALONE_OSX || UNITY_EDITOR_OSX || UNITY_STANDALONE_LINUX || UNITY_EDITOR_LINUX || UNITY_EMBEDDED_LINUX)
        internal const string UnityPlugin = "libVLCUnityPlugin";
#elif UNITY_IOS
        internal const string UnityPlugin = "@rpath/VLCUnityPlugin.framework/VLCUnityPlugin";
#else
        internal const string UnityPlugin = "VLCUnityPlugin";
#endif

#if UNITY_STANDALONE_LINUX || UNITY_EDITOR_LINUX || UNITY_EMBEDDED_LINUX
        internal static string LibVLCDirectory
        {
            get
            {
#if UNITY_EDITOR_LINUX
                // In the Editor, plugins live under the Assets tree
                return System.IO.Path.Combine(Application.dataPath, "VLCUnity", "Plugins", "Linux", "x86_64");
#else
                // In a standalone build, Unity flattens all native plugins into
                // <app>_Data/Plugins/ (no x86_64 subdirectory).
                return System.IO.Path.Combine(Application.dataPath, "Plugins");
#endif
            }
        }
#endif

        [DllImport(UnityPlugin, CallingConvention = CallingConvention.Cdecl, EntryPoint = "libvlc_unity_set_color_space")]
        static extern void SetColorSpace(UnityColorSpace colorSpace);

        [DllImport(UnityPlugin, CallingConvention = CallingConvention.Cdecl)]
        static extern IntPtr GetRenderEventFunc();

        enum UnityColorSpace
        {
            Gamma = 0,
            Linear = 1,
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        static void OnBeforeSceneLoadRuntimeMethod()
        {
            OnQuit();
            _rendererCleanupPending = TextureHelper.HasRetiredRenderers();
            _rendererCleanupNotBeforeFrame = Time.frameCount;
#if UNITY_STANDALONE_LINUX || UNITY_EDITOR_LINUX || UNITY_EMBEDDED_LINUX
            var libDir = LibVLCDirectory;
            var pluginPath = libDir + "/vlc/plugins";
            System.Environment.SetEnvironmentVariable("VLC_PLUGIN_PATH", pluginPath);
            Debug.Log("[VLC] Set VLC_PLUGIN_PATH to " + pluginPath);
#endif
          //  Debug.Log("UnityEngine.QualitySettings.activeColorSpace: " + PlayerColorSpace);
            SetColorSpace(PlayerColorSpace);
#if UNITY_ANDROID || UNITY_IOS || UNITY_STANDALONE_LINUX || UNITY_EDITOR_LINUX || UNITY_EMBEDDED_LINUX
            GL.IssuePluginEvent(GetRenderEventFunc(), 1);
#endif
#if UNITY_EDITOR_LINUX
            // A Play Mode domain can end before another onBeforeRender call.
            // Queue enough ordered render-thread passes at the start of the
            // next domain to retire the previous generation before playback
            // begins importing replacement OpenGL textures.
            if (_rendererCleanupPending)
            {
                for (int pass = 0; pass < 3; ++pass)
                    TextureHelper.QueueRendererCleanupEvent();
            }
#endif
            Application.onBeforeRender += PumpRendererCleanup;
            Application.quitting += OnQuit;
        }

        internal static void RequestRendererCleanup()
        {
            if (!_rendererCleanupPending)
                _rendererCleanupNotBeforeFrame = Time.frameCount + 1;
            _rendererCleanupPending = true;
        }

        static void PumpRendererCleanup()
        {
            // MediaPlayer.Dispose() can retire a native renderer without going
            // through VLCMediaPlayer.DestroyMediaPlayer(). Polling here keeps
            // direct LibVLCSharp and preload disposal paths drainable.
            if (!_rendererCleanupPending)
            {
                if (!TextureHelper.HasRetiredRenderers())
                    return;
                _rendererCleanupPending = true;
                _rendererCleanupNotBeforeFrame = Time.frameCount + 1;
            }

            // Destroy(Texture) is deferred until the end of the frame. Waiting
            // one complete frame keeps the external wrapper alive until Unity
            // has stopped referencing the plugin-owned OpenGL name.
            if (Time.frameCount < _rendererCleanupNotBeforeFrame)
                return;

            if (!TextureHelper.HasRetiredRenderers())
            {
                _rendererCleanupPending = false;
                _rendererCleanupNotBeforeFrame = -1;
                return;
            }

            TextureHelper.QueueRendererCleanupEvent();
        }

        internal static void OnQuit()
        {
            Application.onBeforeRender -= PumpRendererCleanup;
            Application.quitting -= OnQuit;
        }

        static UnityColorSpace PlayerColorSpace => QualitySettings.activeColorSpace == 0 ? UnityColorSpace.Gamma : UnityColorSpace.Linear;
    }
}
