#if VLC_UNITY_GRAPHICS_TEST_PLAYER && !UNITY_EDITOR
using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using LibVLCSharp;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace LibVLCSharp.Tests
{
    // Only compiled into the dedicated CLI test player, never a normal build.
    [DefaultExecutionOrder(32000)]
    public sealed class LinuxGraphicsProbe : MonoBehaviour
    {
        [Serializable]
        sealed class Result
        {
            public int schema = 1;
            public bool passed;
            public string message;
            public string api;
            public string initialization;
            public int binding; // 0 = not applicable (Vulkan), 1 = GLX, 2 = EGL
            public int colors; // bitmask: red=1, green=2, blue=4
            public int samples;
            public string[] libraries;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate void RenderEvent(int eventId);

        [DllImport("libGL.so.1", CallingConvention = CallingConvention.Cdecl)]
        static extern IntPtr glXGetCurrentContext();
        [DllImport("libEGL.so.1", CallingConvention = CallingConvention.Cdecl)]
        static extern IntPtr eglGetCurrentContext();

        static readonly RenderEvent ObserveContext = CaptureContext;
        static int _binding;
        static int _captures;
        static string _captureError;

        [AOT.MonoPInvokeCallback(typeof(RenderEvent))]
        static void CaptureContext(int eventId)
        {
            // Render thread: no Unity object/API access, no GL state changes.
            try
            {
                int binding = (glXGetCurrentContext() != IntPtr.Zero ? 1 : 0) |
                              (eglGetCurrentContext() != IntPtr.Zero ? 2 : 0);
                Interlocked.Exchange(ref _binding, binding);
                Interlocked.Increment(ref _captures);
            }
            catch (Exception exception)
            {
                _captureError = exception.ToString();
            }
        }

        readonly Stopwatch _clock = new Stopwatch();
        readonly Result _result = new Result();
        VLCMediaPlayer _player;
        Texture2D _readback;
        string _directory;
        string _logFile;
        string _expectedBridge;
        string _expectedApi;
        int _expectedBinding;
        IntPtr _renderEvent;
        double _nextSample;
        string _firstError;
        bool _finished;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterAssembliesLoaded)]
        static void Install()
        {
            if (Argument("-vlc-test-output") == null)
                return;
            Application.runInBackground = true;
            var root = new GameObject("VLC Linux graphics CLI test");
            DontDestroyOnLoad(root);
            root.AddComponent<LinuxGraphicsProbe>().Initialize();
        }

        void Initialize()
        {
            _directory = Argument("-vlc-test-output");
            _logFile = Argument("-logFile");
            _expectedBridge = Argument("-vlc-test-bridge");
            _expectedApi = Argument("-vlc-test-api");
            _expectedBinding = int.Parse(Argument("-vlc-test-binding"));
            _result.initialization = Argument("-vlc-test-init");
            _clock.Start();
            Application.logMessageReceivedThreaded += OnLog;
            try
            {
                _renderEvent = Marshal.GetFunctionPointerForDelegate(ObserveContext);
                WriteVideo(Path.Combine(_directory, "colors.y4m"));
                // Cold creation deliberately precedes OnLoad's BeforeSceneLoad
                // render event. Supply the same runtime module path beforehand.
                Environment.SetEnvironmentVariable("VLC_PLUGIN_PATH",
                    Path.Combine(Application.dataPath, "Plugins", "vlc", "plugins"));
                if (_result.initialization == "cold")
                    CreatePlayer();
            }
            catch (Exception exception)
            {
                Finish(false, exception.ToString());
            }
        }

        IEnumerator Start()
        {
            var cameraObject = new GameObject("Graphics test camera");
            cameraObject.AddComponent<Camera>().cullingMask = 0;
            _readback = new Texture2D(16, 16, TextureFormat.RGBA32, false);
            // Read only after Unity finishes rendering the frame and processes
            // the plugin's queued texture work, not halfway through Update.
            while (!_finished)
            {
                yield return new WaitForEndOfFrame();
                try { CheckFrame(); }
                catch (Exception exception) { Finish(false, exception.ToString()); }
            }
        }

        void OnLog(string message, string stack, LogType type)
        {
            if (type == LogType.Error || type == LogType.Exception || type == LogType.Assert)
                Interlocked.CompareExchange(ref _firstError, message, null);
        }

        void CheckFrame()
        {
            _result.api = SystemInfo.graphicsDeviceType.ToString();
            if (_result.api != _expectedApi)
            {
                Finish(false, $"Expected API {_expectedApi}, got {_result.api}");
                return;
            }
            if (_firstError != null || _captureError != null)
            {
                Finish(false, _firstError ?? _captureError);
                return;
            }
            if (_clock.Elapsed.TotalSeconds > 20)
            {
                Finish(false, $"Timed out: playerCreated={_player != null}, " +
                    $"outputCreated={_player != null && _player.OutputTexture != null}, " +
                    $"colors={_result.colors}, captures={Volatile.Read(ref _captures)}");
                return;
            }
            if (_player == null)
            {
                // The warm test must actually see the native readiness marker;
                // an arbitrary frame delay can accidentally test the cold path.
                string log = File.ReadAllText(_logFile);
                string ready = _expectedBridge == "glx"
                    ? "[GLX] retrieved Unity context"
                    : "[EGL-Linux] Unity GL context is available";
                if (!log.Contains(ready))
                    return;
                CreatePlayer();
            }

            // Only OpenGL has a Unity GL context to observe. Do not introduce
            // an unconfigured OpenGL observation event into the Vulkan queue.
            if (_expectedApi != "Vulkan")
            {
                // After VLCMediaPlayer's Update: observe after plugin work,
                // including deferred initialization on Unity's render thread.
                GL.IssuePluginEvent(_renderEvent, 0);
                if (Volatile.Read(ref _captures) == 0)
                    return;
                _result.binding = Volatile.Read(ref _binding);
                if (_result.binding != _expectedBinding)
                {
                    Finish(false, $"Unity context changed/lost: expected binding " +
                        $"{_expectedBinding}, got {_result.binding} (GLX=1, EGL=2)");
                    return;
                }
            }
            if (_clock.Elapsed.TotalSeconds < _nextSample)
                return;
            _nextSample = _clock.Elapsed.TotalSeconds + 0.15;
            var output = _player.OutputTexture;
            if (output == null || !output.IsCreated())
                return;
            if (output.width != 64 || output.height != 64)
            {
                Finish(false, $"Expected 64x64 fixture texture, got {output.width}x{output.height}");
                return;
            }
            var previous = RenderTexture.active;
            try
            {
                RenderTexture.active = output;
                _readback.ReadPixels(new Rect(24, 24, 16, 16), 0, 0, false);
                _readback.Apply(false, false);
            }
            finally { RenderTexture.active = previous; }

            int[] counts = new int[3];
            foreach (Color32 pixel in _readback.GetPixels32())
            {
                if (pixel.r > 120 && pixel.r > pixel.g + 50 && pixel.r > pixel.b + 50) ++counts[0];
                if (pixel.g > 120 && pixel.g > pixel.r + 50 && pixel.g > pixel.b + 50) ++counts[1];
                if (pixel.b > 120 && pixel.b > pixel.r + 50 && pixel.b > pixel.g + 50) ++counts[2];
            }
            for (int color = 0; color < 3; ++color)
                if (counts[color] >= 240) _result.colors |= 1 << color;
            ++_result.samples;
            if (_result.colors == 7)
                Finish(true, "64x64 Unity texture reproduced red, green and blue video frames");
        }

        void CreatePlayer()
        {
            var root = new GameObject("Local color video");
            DontDestroyOnLoad(root);
            root.SetActive(false);
            _player = root.AddComponent<VLCMediaPlayer>();
            _player.playOnAwake = false;
            root.SetActive(true); // Awake constructs the native MediaPlayer.
            Debug.Log("[GraphicsTest] player-created " + _result.initialization);
            _player.Open(new Uri(Path.Combine(_directory, "colors.y4m")).AbsoluteUri);
        }

        void Finish(bool passed, string message)
        {
            if (_finished) return;
            _finished = true;
            _result.passed = passed;
            _result.message = message;
            _result.api = SystemInfo.graphicsDeviceType.ToString();
            _result.binding = Volatile.Read(ref _binding);
            _result.libraries = File.ReadLines("/proc/self/maps")
                .Where(line => line.Contains("/libVLCUnityPlugin.so") ||
                               line.Contains("/libvlc.so") || line.Contains("/libvlccore.so"))
                .Select(line => line.Substring(line.IndexOf('/'))).Distinct().ToArray();
            // Preserve the result even if native shutdown later stalls/crashes.
            File.WriteAllText(Path.Combine(_directory, "result.json"), JsonUtility.ToJson(_result, true));
            Debug.Log("[GraphicsTest] " + (passed ? "PASS " : "FAIL ") + message);
            Application.logMessageReceivedThreaded -= OnLog;
            Application.Quit(passed ? 0 : 1);
        }

        static string Argument(string name)
        {
            string[] args = Environment.GetCommandLineArgs();
            int index = Array.IndexOf(args, name);
            return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
        }

        static void WriteVideo(string path)
        {
            // Lossless, deterministic YUV4MPEG2: 30 seconds at 10 fps, changing
            // solid primary every 0.5s. No ffmpeg, downloads or compressed codecs.
            byte[][] colors = { new byte[] { 81, 90, 240 },
                                new byte[] { 145, 54, 34 }, new byte[] { 41, 240, 110 } };
            using (var stream = File.Create(path))
            using (var writer = new BinaryWriter(stream))
            {
                writer.Write(Encoding.ASCII.GetBytes("YUV4MPEG2 W64 H64 F10:1 Ip A1:1 C420jpeg\n"));
                for (int frame = 0; frame < 300; ++frame)
                {
                    writer.Write(Encoding.ASCII.GetBytes("FRAME\n"));
                    byte[] color = colors[(frame / 5) % 3];
                    for (int plane = 0; plane < 3; ++plane)
                        writer.Write(Enumerable.Repeat(color[plane], plane == 0 ? 4096 : 1024).ToArray());
                }
            }
        }
    }
}
#endif
