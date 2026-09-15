using NUnit.Framework;

namespace LibVLCSharp.Tests
{
    // Fast tests for the test oracle itself: these need neither a GPU nor LibVLC.
    public sealed class LinuxGraphicsAssertionsTests
    {
        const string Result = "{\"schema\":1,\"passed\":true,\"api\":\"OpenGLCore\"," +
            "\"initialization\":\"cold\",\"binding\":1,\"colors\":7,\"samples\":3," +
            "\"libraries\":[\"/p/libVLCUnityPlugin.so\",\"/p/libvlc.so.12\",\"/p/libvlccore.so.9\"]}";
        const string Log = "Selected window backend: x11\n" +
            "[Linux] selected GLX OpenGL interop backend\n" +
            "[GraphicsTest] player-created cold\n[GLX] retrieved Unity context\n";

        [Test]
        public void AcceptsMatchingPixelsApiContextAndPath()
        {
            Assert.That(LinuxGraphicsAssertions.Validate(Result, Log, "OpenGLCore", "glx", "cold", 1, "x11", false, true), Is.Empty);
        }

        [Test]
        public void AcceptsWarmEglOnNativeWayland()
        {
            string json = Result.Replace("OpenGLCore", "OpenGLES3").Replace("cold", "warm").Replace("\"binding\":1", "\"binding\":2");
            string log = "WAYLAND INTERFACE: wl_compositor\n[Linux] selected EGL OpenGL interop backend\n" +
                         "[EGL-Linux] Unity GL context is available\n[GraphicsTest] player-created warm\n";
            Assert.That(LinuxGraphicsAssertions.Validate(json, log, "OpenGLES3", "egl", "warm", 2, "wayland", true), Is.Empty);
        }

        [Test]
        public void AcceptsVulkanWithGlBindingNotApplicable()
        {
            string json = Result.Replace("OpenGLCore", "Vulkan").Replace("\"binding\":1", "\"binding\":0");
            string log = "Selected window backend: x11\n" +
                         "[Vulkan-Linux] renderer initialized with plugin-owned queue submission\n";
            Assert.That(LinuxGraphicsAssertions.Validate(json, log, "Vulkan", "vulkan", "cold", 0, "x11", false), Is.Empty);
        }

        [TestCase("")]
        [TestCase("not json")]
        [TestCase("{}")]
        public void InitializationWithoutACompleteResultIsNotSuccess(string json)
        {
            Assert.That(LinuxGraphicsAssertions.Validate(json, Log, "OpenGLCore", "glx", "cold", 1, "x11", false), Is.Not.Empty);
        }

        [TestCase("\"colors\":7", "\"colors\":1")]
        [TestCase("\"samples\":3", "\"samples\":0")]
        [TestCase("\"binding\":1", "\"binding\":0")]
        [TestCase("\"OpenGLCore\"", "\"Vulkan\"")]
        [TestCase("\"passed\":true", "\"passed\":false")]
        public void RejectsInvalidPlaybackEvidence(string from, string to)
        {
            Assert.That(LinuxGraphicsAssertions.Validate(Result.Replace(from, to), Log,
                "OpenGLCore", "glx", "cold", 1, "x11", false), Is.Not.Empty);
        }

        [Test]
        public void RejectsBackendOrDisplayFallback()
        {
            Assert.That(LinuxGraphicsAssertions.Validate(Result, Log, "OpenGLCore", "egl", "cold", 2, "wayland", false), Is.Not.Empty);
        }

        [Test]
        public void WarmRunMustActuallyCaptureBeforeConstruction()
        {
            Assert.That(LinuxGraphicsAssertions.Validate(Result.Replace("cold", "warm"), Log,
                "OpenGLCore", "glx", "warm", 1, "x11", false), Is.Not.Empty);
        }

        [Test]
        public void RejectsContextSwitchFailureAfterSuccessfulPlayback()
        {
            Assert.That(LinuxGraphicsAssertions.Validate(Result, Log + "[EGL] make current failed: 3002\n",
                "OpenGLCore", "glx", "cold", 1, "x11", false), Is.Not.Empty);
        }

        [Test]
        public void RejectsDuplicateMappedBridgeCopies()
        {
            string json = Result.Replace("\"/p/libVLCUnityPlugin.so\"",
                "\"/p/libVLCUnityPlugin.so\",\"/p/x64/libVLCUnityPlugin.so\"");
            Assert.That(LinuxGraphicsAssertions.Validate(json, Log,
                "OpenGLCore", "glx", "cold", 1, "x11", false, true),
                Has.Some.Contains("Expected one mapped libVLCUnityPlugin.so; got 2"));
        }
    }
}
