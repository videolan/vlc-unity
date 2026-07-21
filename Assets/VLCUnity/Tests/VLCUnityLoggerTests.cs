using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Serialization;
using UnityEngine.TestTools;

namespace LibVLCSharp.Tests
{
    public class VLCUnityLoggerTests
    {
        private string _testDirectory;
        private string _logFilePath;
        private VLCLogSettings _settings;

        [SetUp]
        public void SetUp()
        {
            _testDirectory = Path.Combine(Application.temporaryCachePath, "VLCLoggerTests");

            if (Directory.Exists(_testDirectory))
                Directory.Delete(_testDirectory, true);

            Directory.CreateDirectory(_testDirectory);
            _logFilePath = Path.Combine(_testDirectory, "test_log.txt");

            _settings = ScriptableObject.CreateInstance<VLCLogSettings>();
            _settings.writeToUnityConsole = false;
            _settings.writeToFile = true;
            _settings.logFilePath = _logFilePath;
        }

        [TearDown]
        public void TearDown()
        {
            VLCUnityLogger.ShutdownFileLoggingForTests();
            VLCUnityLogger._settings = null;

            if (_settings != null)
                UnityEngine.Object.DestroyImmediate(_settings);

            if (Directory.Exists(_testDirectory))
                Directory.Delete(_testDirectory, true);
        }

        [Test]
        public void FlushesAcceptedMessagesAcrossBatchesInOrderOnShutdown()
        {
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);

            string[] messages = Enumerable.Range(0, 1000).Select(index => $"Message {index}").ToArray();
            foreach (string message in messages)
                VLCUnityLogger.Log(message);

            VLCUnityLogger.ShutdownFileLoggingForTests();

            Assert.That(File.ReadAllLines(_logFilePath), Is.EqualTo(messages));
        }

        [Test]
        public void PreviousSessionModePreservesOnlyThePreviousSession()
        {
            _settings.rotationMode = LogRotationMode.PreviousSession;
            File.WriteAllText(_logFilePath, "previous session");
            File.WriteAllText(Path.Combine(_testDirectory, "test_log-prev.txt"), "stale session");

            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("current session");
            VLCUnityLogger.ShutdownFileLoggingForTests();

            Assert.That(File.ReadAllText(Path.Combine(_testDirectory, "test_log-prev.txt")), Is.EqualTo("previous session"));
            Assert.That(File.ReadAllText(_logFilePath), Does.Contain("current session"));
        }

        [Test]
        public void FileSizeModeRotatesAndEnforcesRetentionLimit()
        {
            _settings.rotationMode = LogRotationMode.FileSize;
            _settings.maxFileSizeMB = 1;
            _settings.maxRetainedFiles = 2;

            File.WriteAllBytes(_logFilePath, new byte[1024 * 1024 + 1]);
            File.WriteAllText(Path.Combine(_testDirectory, "test_log_1.txt"), "older session");
            File.WriteAllText(Path.Combine(_testDirectory, "test_log_2.txt"), "stale session");

            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("current message");
            VLCUnityLogger.ShutdownFileLoggingForTests();

            Assert.That(new FileInfo(Path.Combine(_testDirectory, "test_log_1.txt")).Length, Is.GreaterThan(1024 * 1024));
            Assert.That(File.ReadAllText(Path.Combine(_testDirectory, "test_log_2.txt")), Is.EqualTo("older session"));
            Assert.That(File.Exists(Path.Combine(_testDirectory, "test_log_3.txt")), Is.False);
            Assert.That(File.ReadAllText(_logFilePath), Does.Contain("current message"));
        }

        [Test]
        public void FileSizeModeCanDisableRetention()
        {
            _settings.rotationMode = LogRotationMode.FileSize;
            _settings.maxFileSizeMB = 1;
            _settings.maxRetainedFiles = 0;
            File.WriteAllBytes(_logFilePath, new byte[1024 * 1024 + 1]);

            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("current message");
            VLCUnityLogger.ShutdownFileLoggingForTests();

            Assert.That(File.Exists(Path.Combine(_testDirectory, "test_log_1.txt")), Is.False);
            Assert.That(File.ReadAllText(_logFilePath), Does.Contain("current message"));
        }

        [TestCase(LogRotationInterval.Hourly, "yyyy-MM-dd_HH")]
        [TestCase(LogRotationInterval.Daily, "yyyy-MM-dd")]
        [TestCase(LogRotationInterval.Monthly, "yyyy-MM")]
        public void TimeIntervalModeUsesTheConfiguredFileName(LogRotationInterval interval, string format)
        {
            _settings.rotationMode = LogRotationMode.TimeInterval;
            _settings.rotationInterval = interval;
            DateTime beforeWrite = DateTime.Now;

            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("time-based message");
            VLCUnityLogger.ShutdownFileLoggingForTests();

            DateTime afterWrite = DateTime.Now;
            string beforePath = GetTimeBasedPath(beforeWrite, format);
            string afterPath = GetTimeBasedPath(afterWrite, format);
            string actualPath = File.Exists(beforePath) ? beforePath : afterPath;

            Assert.That(File.Exists(actualPath), Is.True);
            Assert.That(File.ReadAllText(actualPath), Does.Contain("time-based message"));
        }

        [Test]
        public void ReinitializationFlushesThePreviousWriter()
        {
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("first writer");

            var replacementSettings = ScriptableObject.CreateInstance<VLCLogSettings>();
            replacementSettings.writeToFile = true;
            replacementSettings.logFilePath = Path.Combine(_testDirectory, "replacement.txt");

            try
            {
                VLCUnityLogger.InitializeFileLoggingForTests(replacementSettings);
                VLCUnityLogger.Log("second writer");
                VLCUnityLogger.ShutdownFileLoggingForTests();

                Assert.That(File.ReadAllText(_logFilePath), Does.Contain("first writer"));
                Assert.That(File.ReadAllText(replacementSettings.logFilePath), Does.Contain("second writer"));
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(replacementSettings);
            }
        }

        [Test]
        public void SettingsChangesApplyWithoutReinitializing()
        {
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("before change");

            _settings.writeToFile = false;
            VLCUnityLogger.ReapplySettings(_settings);

            VLCUnityLogger.Log("after change");
            VLCUnityLogger.ShutdownFileLoggingForTests();

            string contents = File.ReadAllText(_logFilePath);
            Assert.That(contents, Does.Contain("before change"));
            Assert.That(contents, Does.Not.Contain("after change"));
        }

        [Test]
        public void ReapplyingForeignSettingsIsIgnored()
        {
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);

            var foreignSettings = ScriptableObject.CreateInstance<VLCLogSettings>();
            foreignSettings.writeToFile = false;

            try
            {
                VLCUnityLogger.ReapplySettings(foreignSettings);
                VLCUnityLogger.Log("still persisted");
                VLCUnityLogger.ShutdownFileLoggingForTests();

                Assert.That(File.ReadAllText(_logFilePath), Does.Contain("still persisted"));
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(foreignSettings);
            }
        }

        [Test]
        public void DisabledFileSinkDoesNotCreateAFile()
        {
            _settings.writeToFile = false;

            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            VLCUnityLogger.Log("not persisted");
            VLCUnityLogger.ShutdownFileLoggingForTests();

            Assert.That(File.Exists(_logFilePath), Is.False);
        }

        [Test]
        public void InvalidPathDisablesFileLoggingWithoutThrowing()
        {
            _settings.logFilePath = "\0invalid";
            LogAssert.Expect(LogType.Warning, new Regex("VLC file logging is disabled"));

            Assert.DoesNotThrow(() => VLCUnityLogger.InitializeFileLoggingForTests(_settings));
            Assert.DoesNotThrow(() => VLCUnityLogger.Log("not persisted"));
        }

        [Test]
        public void LogReceivedForwardsMessageAndIsolatesSubscriberExceptions()
        {
            string receivedMessage = null;
            Action<string> throwingHandler = _ => throw new InvalidOperationException("subscriber failed");
            Action<string> recordingHandler = message => receivedMessage = message;

            VLCUnityLogger.LogReceived += throwingHandler;
            VLCUnityLogger.LogReceived += recordingHandler;
            LogAssert.Expect(LogType.Exception, new Regex("InvalidOperationException: subscriber failed"));

            try
            {
                VLCUnityLogger.Log("managed message");
            }
            finally
            {
                VLCUnityLogger.LogReceived -= throwingHandler;
                VLCUnityLogger.LogReceived -= recordingHandler;
            }

            Assert.That(receivedMessage, Is.EqualTo("managed message"));
        }

        [Test]
        public void RemovingLastSubscriberUnregistersNativeCallback()
        {
            _settings.writeToFile = false;
            _settings.captureEngineLogs = true;
            Action<string> handler = _ => { };
            bool subscribed = false;
            bool registrationObserved = false;
            var unregistered = new ManualResetEventSlim();

            VLCUnityLogger.OnQuit();
            VLCUnityLogger.NativeCallbackSetterForTests = enabled =>
            {
                if (enabled)
                    registrationObserved = true;
                else
                    unregistered.Set();
            };

            try
            {
                VLCUnityLogger.Initialize();
                VLCUnityLogger.InitializeFileLoggingForTests(_settings);

                VLCUnityLogger.LogReceived += handler;
                subscribed = true;

                Assert.That(registrationObserved, Is.True);
                Assert.That(VLCUnityLogger.ShouldCaptureNativeLogs(), Is.True);

                VLCUnityLogger.LogReceived -= handler;
                subscribed = false;

                Assert.That(unregistered.Wait(TimeSpan.FromSeconds(1)), Is.True);
                Assert.That(VLCUnityLogger.ShouldCaptureNativeLogs(), Is.False);
            }
            finally
            {
                if (subscribed)
                    VLCUnityLogger.LogReceived -= handler;

                VLCUnityLogger.OnQuit();
                VLCUnityLogger.NativeCallbackSetterForTests = null;
                unregistered.Dispose();
            }
        }

        [Test]
        public void MessageReachesEveryConfiguredOutput()
        {
            const string expectedMessage = "[VLCMediaPlayer:Player A] player started";
            string eventMessage = null;
            string unityEventMessage = null;
            Action<string> eventHandler = message => eventMessage = message;

            _settings.writeToUnityConsole = true;
            _settings.onLogReceived = new UnityEngine.Events.UnityEvent<string>();
            _settings.onLogReceived.AddListener(message => unityEventMessage = message);

            VLCUnityLogger.LogReceived += eventHandler;
            LogAssert.Expect(LogType.Log, new Regex("VLCMediaPlayer:Player A.*player started"));

            try
            {
                VLCUnityLogger.InitializeFileLoggingForTests(_settings, new ImmediateSynchronizationContext());
                VLCUnityLogger.Log(expectedMessage);
                VLCUnityLogger.ShutdownFileLoggingForTests();
            }
            finally
            {
                VLCUnityLogger.LogReceived -= eventHandler;
            }

            Assert.That(File.ReadAllText(_logFilePath), Does.Contain(expectedMessage));
            Assert.That(unityEventMessage, Is.EqualTo(expectedMessage));
            Assert.That(eventMessage, Is.EqualTo(expectedMessage));
        }

        [Test]
        public void DefaultSettingsShowComponentDiagnosticsWithoutEnablingNoisySources()
        {
            var defaultSettings = ScriptableObject.CreateInstance<VLCLogSettings>();

            try
            {
                Assert.That(defaultSettings.writeToUnityConsole, Is.True);
                Assert.That(defaultSettings.writeToFile, Is.False);
                Assert.That(defaultSettings.captureEngineLogs, Is.False);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(defaultSettings);
            }
        }

        [Test]
        public void NativeLogsRequireAnEnabledSourceAndOutput()
        {
            _settings.writeToFile = false;
            _settings.writeToUnityConsole = false;
            _settings.captureEngineLogs = true;

            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            Assert.That(VLCUnityLogger.ShouldCaptureNativeLogs(), Is.False);

            _settings.writeToUnityConsole = true;
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            Assert.That(VLCUnityLogger.ShouldCaptureNativeLogs(), Is.True);

            _settings.captureEngineLogs = false;
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);
            Assert.That(VLCUnityLogger.ShouldCaptureNativeLogs(), Is.False);
        }

        [Test]
        public void LegacyLogToConsoleAliasMirrorsEnableDiagnostics()
        {
            var playerObject = new GameObject("Legacy logging compatibility");
            playerObject.SetActive(false);
            var player = playerObject.AddComponent<VLCMediaPlayer>();

            try
            {
#pragma warning disable CS0618
                player.logToConsole = true;
                Assert.That(player.enableDiagnostics, Is.True);

                player.enableDiagnostics = false;
                Assert.That(player.logToConsole, Is.False);
#pragma warning restore CS0618
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(playerObject);
            }
        }

        [Test]
        public void LibVLCEngineDebugLogsDefaultToDisabled()
        {
            var playerObject = new GameObject("LibVLC debug logging defaults");
            playerObject.SetActive(false);
            var player = playerObject.AddComponent<VLCMediaPlayer>();

            try
            {
                Assert.That(player.enableLibVLCDebugLogs, Is.False);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(playerObject);
            }
        }

        [Test]
        public void ComponentEngineDebugLogsEnableLibVLCCapture()
        {
            _settings.captureEngineLogs = false;
            VLCUnityLogger.InitializeFileLoggingForTests(_settings);

            Assert.That(VLCUnityLogger.ShouldCaptureLibVLCLogs(false), Is.False);
            Assert.That(VLCUnityLogger.ShouldCaptureLibVLCLogs(true), Is.True);
        }

        [Test]
        public void ShutdownClearsLogReceivedSubscribers()
        {
            int receivedCount = 0;
            Action<string> handler = _ => receivedCount++;
            VLCUnityLogger.LogReceived += handler;

            try
            {
                VLCUnityLogger.OnQuit();
                VLCUnityLogger.Log("after shutdown");

                Assert.That(receivedCount, Is.Zero);
            }
            finally
            {
                VLCUnityLogger.LogReceived -= handler;
            }
        }

        [Test]
        public void LegacyPlaylistLoggingEnablesControllerAndChildDiagnostics()
        {
            var playlistObject = new GameObject("Legacy playlist logging compatibility");
            playlistObject.SetActive(false);
            var controller = playlistObject.AddComponent<VLCPlaylistController>();

            try
            {
                var controllerType = typeof(VLCPlaylistController);

                FieldInfo legacyField = controllerType.GetField("legacyLogToConsole", BindingFlags.Instance | BindingFlags.NonPublic);
                FieldInfo diagnosticsField = controllerType.GetField("enableDiagnostics", BindingFlags.Instance | BindingFlags.NonPublic);
                FieldInfo childDiagnosticsField = controllerType.GetField("enableChildPlayerDiagnostics", BindingFlags.Instance | BindingFlags.NonPublic);

                Assert.That(legacyField, Is.Not.Null);
                Assert.That(diagnosticsField, Is.Not.Null);
                Assert.That(childDiagnosticsField, Is.Not.Null);

                var formerName = legacyField.GetCustomAttribute<FormerlySerializedAsAttribute>();
                Assert.That(formerName?.oldName, Is.EqualTo("logToConsole"));

                legacyField.SetValue(controller, true);
                ((ISerializationCallbackReceiver)controller).OnAfterDeserialize();

                Assert.That(diagnosticsField.GetValue(controller), Is.True);
                Assert.That(childDiagnosticsField.GetValue(controller), Is.True);
                Assert.That(legacyField.GetValue(controller), Is.False);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(playlistObject);
            }
        }

        private string GetTimeBasedPath(DateTime time, string format)
        {
            return Path.Combine(_testDirectory, $"test_log_{time.ToString(format)}.txt");
        }

        private sealed class ImmediateSynchronizationContext : SynchronizationContext
        {
            public override void Post(SendOrPostCallback callback, object state)
            {
                callback(state);
            }
        }
    }
}
