using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using Debug = UnityEngine.Debug;

namespace LibVLCSharp.Tests
{
    /// <summary>
    /// Unity CLI EditMode tests which build and launch real Linux players.
    /// Opt in with VLC_UNITY_RUN_GRAPHICS_TESTS=1; see TESTING.md.
    /// </summary>
    public sealed class LinuxGraphicsCliTests
    {
        string _output;
        string _executable;
        bool _embedded;

        public static IEnumerable<TestCaseData> Paths()
        {
            yield return new TestCaseData("x11", "auto", "cold").SetName("Auto_X11_Cold");
            yield return new TestCaseData("wayland", "auto", "cold").SetName("Auto_Wayland_Cold");
            foreach (string phase in new[] { "cold", "warm" })
            {
                yield return new TestCaseData("x11", "glx", phase).SetName("GLX_X11_" + phase);
                yield return new TestCaseData("x11", "egl", phase).SetName("EGL_X11_" + phase);
                yield return new TestCaseData("wayland", "egl", phase).SetName("EGL_Wayland_" + phase);
            }
            yield return new TestCaseData("x11", "vulkan", "cold").SetName("Vulkan_X11");
            yield return new TestCaseData("wayland", "vulkan", "cold").SetName("Vulkan_Wayland");
        }

        [OneTimeSetUp]
        public void BuildTestPlayer()
        {
            if (Environment.GetEnvironmentVariable("VLC_UNITY_RUN_GRAPHICS_TESTS") != "1")
                Assert.Ignore("Opt in with VLC_UNITY_RUN_GRAPHICS_TESTS=1; this suite builds and launches GPU players.");
            Assert.That(Application.platform, Is.EqualTo(RuntimePlatform.LinuxEditor));
            Assert.That(Application.isBatchMode, Is.True, "Run this build-and-launch suite through Unity CLI.");
            var target = EditorUserBuildSettings.activeBuildTarget;
            Assert.That(target, Is.EqualTo(BuildTarget.StandaloneLinux64).Or.EqualTo(BuildTarget.EmbeddedLinux),
                "Start Unity with -buildTarget Linux64 or -buildTarget EmbeddedLinux.");
            _embedded = target == BuildTarget.EmbeddedLinux;
            string parent = Path.GetFullPath(Environment.GetEnvironmentVariable("VLC_UNITY_GRAPHICS_RESULTS") ??
                                            "build-linux-graphics-tests");
            _output = Path.Combine(parent, target + "-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(_output);
            Debug.Log("Linux graphics test artifacts: " + _output);
            string scenePath = "Assets/__VLCGraphicsTest_" + Guid.NewGuid().ToString("N") + ".unity";
            bool automatic = PlayerSettings.GetUseDefaultGraphicsAPIs(target);
            var apis = PlayerSettings.GetGraphicsAPIs(target);
            var scenes = EditorSceneManager.GetSceneManagerSetup();
            try
            {
                // The CLI Test Runner starts with an untitled scene; Unity will
                // not create an additive scene beside it. No production scene
                // file is saved or overwritten by replacing that in-memory scene.
                var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
                Assert.That(EditorSceneManager.SaveScene(scene, scenePath), Is.True);
                PlayerSettings.SetUseDefaultGraphicsAPIs(target, false);
                PlayerSettings.SetGraphicsAPIs(target, new[] {
                    _embedded ? GraphicsDeviceType.OpenGLES3 : GraphicsDeviceType.OpenGLCore,
                    GraphicsDeviceType.Vulkan });
                var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
                    scenes = new[] { scenePath }, target = target,
                    locationPathName = Path.Combine(_output, "player", "app"),
                    options = BuildOptions.Development,
                    extraScriptingDefines = new[] { "VLC_UNITY_GRAPHICS_TEST_PLAYER" }
                });
                Assert.That(report.summary.result, Is.EqualTo(BuildResult.Succeeded), "Test player build failed.");
                _executable = _embedded ? Path.Combine(_output, "player", "app", "app")
                                        : Path.Combine(_output, "player", "app");
                Assert.That(File.Exists(_executable), Is.True, "Player executable not found: " + _executable);
            }
            finally
            {
                PlayerSettings.SetGraphicsAPIs(target, apis);
                PlayerSettings.SetUseDefaultGraphicsAPIs(target, automatic);
                if (scenes.Length > 0 && scenes.All(scene => !string.IsNullOrEmpty(scene.path)))
                    EditorSceneManager.RestoreSceneManagerSetup(scenes);
                else
                    EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
                AssetDatabase.DeleteAsset(scenePath); // Only this invocation's unique scene.
            }
        }

        [TestCaseSource(nameof(Paths))]
        public void VideoRemainsInsideUnity(string display, string bridge, string phase)
        {
            RunAndAssert(display, bridge, phase, false);
        }

        [Test]
        public void NativeLibrariesAreLoadedOnce()
        {
            RunAndAssert("x11", "glx", "warm", true);
        }

        void RunAndAssert(string display, string bridge, string phase, bool checkPackaging)
        {
            Assert.That(Environment.GetEnvironmentVariable(display == "x11" ? "DISPLAY" : "WAYLAND_DISPLAY"),
                Is.Not.Null.And.Not.Empty, "The requested display server must be available; no silent fallback/skip.");
            if (display == "wayland" && bridge == "auto")
                Assert.That(Environment.GetEnvironmentVariable("DISPLAY"), Is.Not.Null.And.Not.Empty,
                    "The auto-selection regression specifically requires both display variables set.");
            string directory = Path.Combine(_output, TestContext.CurrentContext.Test.Name);
            Directory.CreateDirectory(directory);
            string log = Path.Combine(directory, "player.log");
            string expectedBridge = bridge == "auto" ? (display == "x11" ? "glx" : "egl") : bridge;
            string api = bridge == "vulkan" ? "Vulkan" : (_embedded ? "OpenGLES3" : "OpenGLCore");
            int binding = bridge == "vulkan" ? 0 : (display == "x11" ? 1 : 2);
            var args = new List<string> {
                bridge == "vulkan" ? "-force-vulkan" : (_embedded ? "-force-gles" : "-force-glcore"),
                "-screen-fullscreen", "0", "-screen-width", "320", "-screen-height", "240",
                "-logFile", log, "-vlc-test-output", directory, "-vlc-test-api", api,
                "-vlc-test-binding", binding.ToString(), "-vlc-test-bridge", expectedBridge,
                "-vlc-test-init", phase
            };
            if (!_embedded && display == "wayland") args.Add("-force-wayland");
            var start = new ProcessStartInfo(_executable, string.Join(" ", args.Select(Quote))) {
                UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true
            };
            start.EnvironmentVariables["SDL_VIDEODRIVER"] = display;
            start.EnvironmentVariables.Remove("VLC_UNITY_LINUX_OPENGL_BACKEND");
            start.EnvironmentVariables.Remove("VLC_UNITY_GLX_FORCE_DMABUF");
            if (bridge == "egl" || bridge == "glx")
                start.EnvironmentVariables["VLC_UNITY_LINUX_OPENGL_BACKEND"] = bridge;
            bool exited;
            bool drained;
            int exitCode;
            using (var process = new Process { StartInfo = start })
            {
                process.Start();
                var stdout = process.StandardOutput.ReadToEndAsync();
                var stderr = process.StandardError.ReadToEndAsync();
                exited = process.WaitForExit(45000);
                if (!exited) process.Kill(); // Only the player started by this test.
                Assert.That(process.WaitForExit(5000), Is.True, "Test process could not be stopped.");
                // A crash helper can inherit stdout/stderr. Never wait forever
                // for its pipes after the actual player has exited.
                drained = Task.WaitAll(new Task[] { stdout, stderr }, 5000);
                File.WriteAllText(Path.Combine(directory, "console.log"),
                    (stdout.IsCompleted ? stdout.GetAwaiter().GetResult() : "[stdout did not close]\n") +
                    (stderr.IsCompleted ? stderr.GetAwaiter().GetResult() : "[stderr did not close]\n"));
                exitCode = process.ExitCode;
            }
            string resultPath = Path.Combine(directory, "result.json");
            string resultJson = File.Exists(resultPath) ? File.ReadAllText(resultPath) : "";
            string playerLog = File.Exists(log) ? File.ReadAllText(log) : "";
            var errors = LinuxGraphicsAssertions.Validate(resultJson, playerLog, api,
                expectedBridge, phase, binding, display, _embedded, checkPackaging);
            if (!exited) errors.Add("Player exceeded 45s wall-clock limit (startup/render/shutdown stall).");
            if (!drained) errors.Add("Player output pipes did not close within 5s after exit.");
            if (exitCode != 0) errors.Add("Player exit code: " + exitCode);
            File.WriteAllLines(Path.Combine(directory, "assertions.txt"), errors.Count == 0 ? new[] { "PASS" } : errors.ToArray());
            Assert.That(errors, Is.Empty, string.Join("\n", errors) + "\nArtifacts: " + directory);
        }

        static string Quote(string value) => "\"" + value.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
    }

    internal static class LinuxGraphicsAssertions
    {
        [Serializable]
        sealed class Result
        {
            public int schema;
            public bool passed;
            public string message;
            public string api;
            public string initialization;
            public int binding;
            public int colors;
            public int samples;
            public string[] libraries;
        }

        internal static List<string> Validate(string json, string log, string api, string bridge,
            string phase, int binding, string display, bool embedded, bool packaging = false)
        {
            var errors = new List<string>();
            if (log.Contains("[EGL] make current failed:") ||
                log.Contains("could not release Unity context"))
                errors.Add("OpenGL context switching failed during playback or cleanup.");
            Result result = null;
            try { if (!string.IsNullOrEmpty(json)) result = JsonUtility.FromJson<Result>(json); }
            catch (Exception exception) { errors.Add("Invalid result JSON: " + exception.Message); }
            if (result == null) errors.Add("No result.json: probe did not complete; initialization alone is not a pass.");
            else
            {
                if (result.schema != 1) errors.Add("Unsupported result schema.");
                if (!result.passed) errors.Add("Playback failed: " + result.message);
                if (result.api != api) errors.Add("Wrong Unity graphics API: " + result.api);
                if (result.initialization != phase) errors.Add("Wrong initialization phase.");
                if (result.binding != binding) errors.Add("Wrong/lost Unity render-thread context.");
                if (result.colors != 7 || result.samples < 3) errors.Add("Texture did not reproduce all three fixture colors.");
                if (packaging)
                    foreach (string library in new[] { "libVLCUnityPlugin.so", "libvlc.so", "libvlccore.so" })
                    {
                        int count = (result.libraries ?? Array.Empty<string>())
                            .Where(path => Path.GetFileName(path).StartsWith(library, StringComparison.Ordinal)).Distinct().Count();
                        if (count != 1) errors.Add($"Expected one mapped {library}; got {count}.");
                    }
            }
            bool actualWayland = embedded ? log.Contains("WAYLAND INTERFACE:")
                                          : log.Contains("Selected window backend: wayland");
            bool actualX11 = embedded ? log.Contains("ScreenInit:") && !actualWayland
                                      : log.Contains("Selected window backend: x11");
            if (display == "wayland" ? !actualWayland : !actualX11)
                errors.Add("Requested display backend was not confirmed in the player log.");
            if (bridge == "vulkan")
            {
                if (!log.Contains("[Vulkan-Linux] renderer initialized with plugin-owned queue submission"))
                    errors.Add("Vulkan interop did not initialize.");
            }
            else
            {
                string selected = "[Linux] selected " + bridge.ToUpperInvariant() + " OpenGL interop backend";
                if (!log.Contains(selected)) errors.Add("Wrong/missing native bridge selection: expected " + bridge);
                string ready = bridge == "glx" ? "[GLX] retrieved Unity context" : "[EGL-Linux] Unity GL context is available";
                int capture = log.IndexOf(ready, StringComparison.Ordinal);
                int creation = log.IndexOf("[GraphicsTest] player-created", StringComparison.Ordinal);
                if (capture < 0 || creation < 0 || (phase == "warm" ? capture > creation : capture < creation))
                    errors.Add("Requested cold/warm initialization order was not exercised.");
            }
            return errors;
        }
    }
}
