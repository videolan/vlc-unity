using NUnit.Framework;
using System.Collections;
using System.IO;
using UnityEngine;
using UnityEngine.TestTools;

namespace LibVLCSharp.Tests
{
    public class VLCUnityLoggerTests
    {
        private string _testDir;
        private string _logFilePath;
        private VLCLogSettings _settings;

        [SetUp]
        public void SetUp()
        {
            _testDir = Path.Combine(Application.temporaryCachePath, "VLCLoggerTests");

            if (Directory.Exists(_testDir))
                Directory.Delete(_testDir, true);

            Directory.CreateDirectory(_testDir);

            _logFilePath = Path.Combine(_testDir, "test_log.txt");

            _settings = ScriptableObject.CreateInstance<VLCLogSettings>();
            _settings.writeToUnityConsole = false;
            _settings.writeToTextFile = true;
            _settings.logFilePath = _logFilePath;

            VLCUnityLogger._settings = _settings;
        }

        [UnityTearDown]
        public IEnumerator TearDown()
        {
            VLCUnityLogger.OnQuit();

            yield return new WaitForSeconds(0.2f);

            if (Directory.Exists(_testDir))
                Directory.Delete(_testDir, true);

            Object.DestroyImmediate(_settings);
            VLCUnityLogger._settings = null;
        }

        [UnityTest]
        public IEnumerator InitializesStandardLogFile()
        {
            _settings.rotationMode = LogRotationMode.Standard;

            VLCUnityLogger.Initialize();
            VLCUnityLogger.RouteLog("Test Standard");

            yield return new WaitForSeconds(0.2f);

            Assert.IsTrue(File.Exists(_logFilePath), "Log file should be created after initialization.");
        }

        [UnityTest]
        public IEnumerator RotatesFileOnSizeLimit()
        {
            _settings.rotationMode = LogRotationMode.Size;
            _settings.maxFileSizeMB = 1;

            File.WriteAllBytes(_logFilePath, new byte[1024 * 1100]);

            VLCUnityLogger.Initialize();
            VLCUnityLogger.RouteLog("Test Size Rotation");

            yield return new WaitForSeconds(0.5f);

            string rotatedPath = Path.Combine(_testDir, "test_log_1.txt");
            Assert.IsTrue(File.Exists(rotatedPath), "Log file should rotate when the size threshold is reached.");
        }

        [UnityTest]
        public IEnumerator FormatsHourlyFileName()
        {
            _settings.rotationMode = LogRotationMode.Time;
            _settings.timeInterval = LogTimeInterval.Hourly;

            VLCUnityLogger.Initialize();
            VLCUnityLogger.RouteLog("Test Hourly Rotation");

            yield return new WaitForSeconds(0.2f);

            string timeSuffix = System.DateTime.Now.ToString("yyyy-MM-dd_HH");
            string expectedFilePath = Path.Combine(_testDir, $"test_log_{timeSuffix}.txt");

            Assert.IsTrue(File.Exists(expectedFilePath), $"Log file should be created with hourly time suffix: {expectedFilePath}");
        }

        [UnityTest]
        public IEnumerator FormatsDailyFileName()
        {
            _settings.rotationMode = LogRotationMode.Time;
            _settings.timeInterval = LogTimeInterval.Daily;

            VLCUnityLogger.Initialize();
            VLCUnityLogger.RouteLog("Test Daily Rotation");

            yield return new WaitForSeconds(0.2f);

            string timeSuffix = System.DateTime.Now.ToString("yyyy-MM-dd");
            string expectedFilePath = Path.Combine(_testDir, $"test_log_{timeSuffix}.txt");

            Assert.IsTrue(File.Exists(expectedFilePath), $"Log file should be created with daily time suffix: {expectedFilePath}");
        }

        [UnityTest]
        public IEnumerator FormatsMonthlyFileName()
        {
            _settings.rotationMode = LogRotationMode.Time;
            _settings.timeInterval = LogTimeInterval.Monthly;

            VLCUnityLogger.Initialize();
            VLCUnityLogger.RouteLog("Test Monthly Rotation");

            yield return new WaitForSeconds(0.2f);

            string timeSuffix = System.DateTime.Now.ToString("yyyy-MM");
            string expectedFilePath = Path.Combine(_testDir, $"test_log_{timeSuffix}.txt");

            Assert.IsTrue(File.Exists(expectedFilePath), $"Log file should be created with monthly time suffix: {expectedFilePath}");
        }
    }
}
