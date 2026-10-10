using System.IO;
using NUnit.Framework;

namespace Videolabs.VLCUnity.Packaging.Tests
{
    public sealed class PluginImporterMetaConverterTests
    {
        const string Header =
            "fileFormatVersion: 2\n" +
            "guid: 028bc65da849c654dbaf6a6e8720f997\n" +
            "PluginImporter:\n" +
            "  externalObjects: {}\n";
        const string Options =
            "  iconMap: {}\n" +
            "  executionOrder: {}\n" +
            "  defineConstraints: []\n" +
            "  isPreloaded: 0\n" +
            "  isOverridable: 0\n" +
            "  isExplicitlyReferenced: 0\n" +
            "  validateReferences: 1\n";
        const string Footer =
            "  userData: \n" +
            "  assetBundleName: \n" +
            "  assetBundleVariant: \n";

        // As written by Unity 6 for the Windows LibVLCSharp.dll (subset of platforms).
        const string Version3 = Header + "  serializedVersion: 3\n" + Options +
            "  platformData:\n" +
            "    Android:\n" +
            "      enabled: 1\n" +
            "      settings: {}\n" +
            "    Any:\n" +
            "      enabled: 0\n" +
            "      settings: {}\n" +
            "    Editor:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        DefaultValueInitialized: true\n" +
            "    Win64:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        CPU: x86_64\n" +
            "    WindowsStoreApps:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        CPU: AnyCPU\n" +
            "    iOS:\n" +
            "      enabled: 1\n" +
            "      settings: {}\n" +
            Footer;

        // The same importer settings as Unity 2022.3 writes them.
        const string Version2 = Header + "  serializedVersion: 2\n" + Options +
            "  platformData:\n" +
            "  - first:\n" +
            "      Android: Android\n" +
            "    second:\n" +
            "      enabled: 1\n" +
            "      settings: {}\n" +
            "  - first:\n" +
            "      Any: \n" +
            "    second:\n" +
            "      enabled: 0\n" +
            "      settings: {}\n" +
            "  - first:\n" +
            "      Editor: Editor\n" +
            "    second:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        DefaultValueInitialized: true\n" +
            "  - first:\n" +
            "      Standalone: Win64\n" +
            "    second:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        CPU: x86_64\n" +
            "  - first:\n" +
            "      Windows Store Apps: WindowsStoreApps\n" +
            "    second:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        CPU: AnyCPU\n" +
            "  - first:\n" +
            "      iPhone: iOS\n" +
            "    second:\n" +
            "      enabled: 1\n" +
            "      settings: {}\n" +
            Footer;

        [Test]
        public void ConvertsVersion3ToTheVersion2Layout()
        {
            Assert.That(PluginImporterMetaConverter.Convert(Version3, out string converted),
                Is.EqualTo(PluginImporterMetaConverter.Result.Converted));
            Assert.That(converted, Is.EqualTo(Version2));
            Assert.That(PluginImporterMetaConverter.IsIncompatible(Version3), Is.True);
            Assert.That(PluginImporterMetaConverter.IsIncompatible(converted), Is.False);
        }

        // One plugin enabled on every platform, saved by Unity 6000.0.37f1 and 2022.3.47f1.
        [Test]
        public void MatchesWhatUnity2022Writes()
        {
            const string fixtures = "Assets/VLCUnityPackaging/Tests/Fixtures/";
            string unity6 = File.ReadAllText(fixtures + "PluginImporter.Unity6.txt");
            string unity2022 = File.ReadAllText(fixtures + "PluginImporter.Unity2022.txt");

            Assert.That(PluginImporterMetaConverter.Convert(unity6, out string converted),
                Is.EqualTo(PluginImporterMetaConverter.Result.Converted));
            Assert.That(converted, Is.EqualTo(unity2022));
        }

        [Test]
        public void KeepsWindowsLineEndings()
        {
            PluginImporterMetaConverter.Convert(Version3.Replace("\n", "\r\n"), out string converted);
            Assert.That(converted, Is.EqualTo(Version2.Replace("\n", "\r\n")));
        }

        [Test]
        public void LeavesVersion2Untouched()
        {
            Assert.That(PluginImporterMetaConverter.Convert(Version2, out string converted),
                Is.EqualTo(PluginImporterMetaConverter.Result.AlreadyCompatible));
            Assert.That(converted, Is.SameAs(Version2));
        }

        [Test]
        public void IgnoresOtherImporters()
        {
            const string meta = "fileFormatVersion: 2\nguid: 0123\nMonoImporter:\n  serializedVersion: 3\n";
            Assert.That(PluginImporterMetaConverter.Convert(meta, out _),
                Is.EqualTo(PluginImporterMetaConverter.Result.NotPluginImporter));
            Assert.That(PluginImporterMetaConverter.IsIncompatible(meta), Is.False);
        }

        [Test]
        public void ConvertsEmptyPlatformData()
        {
            string meta = Header + "  serializedVersion: 3\n" + Options + "  platformData: {}\n" + Footer;
            PluginImporterMetaConverter.Convert(meta, out string converted);
            Assert.That(converted, Is.EqualTo(Header + "  serializedVersion: 2\n" + Options + "  platformData: []\n" + Footer));
        }

        [Test]
        public void RejectsUnknownPlatforms()
        {
            string meta = Version3.Replace("    Win64:\n", "    FuturePlatform:\n");
            var e = Assert.Throws<InvalidDataException>(() => PluginImporterMetaConverter.Convert(meta, out _));
            Assert.That(e.Message, Does.Contain("FuturePlatform"));
        }

        [Test]
        public void RejectsUnknownVersions()
        {
            string meta = Version3.Replace("serializedVersion: 3", "serializedVersion: 4");
            Assert.Throws<InvalidDataException>(() => PluginImporterMetaConverter.Convert(meta, out _));
        }
    }
}
