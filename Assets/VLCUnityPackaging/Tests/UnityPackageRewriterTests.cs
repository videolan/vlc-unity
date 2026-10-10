using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;
using NUnit.Framework;

namespace Videolabs.VLCUnity.Packaging.Tests
{
    public sealed class UnityPackageRewriterTests
    {
        const string PluginGuid = "028bc65da849c654dbaf6a6e8720f997";
        const string ScriptGuid = "5f737374db13fde43bfc9f5e7aeab1ac";

        const string PluginMeta =
            "fileFormatVersion: 2\n" +
            "guid: 028bc65da849c654dbaf6a6e8720f997\n" +
            "PluginImporter:\n" +
            "  serializedVersion: 3\n" +
            "  platformData:\n" +
            "    Any:\n" +
            "      enabled: 0\n" +
            "      settings: {}\n" +
            "    Win64:\n" +
            "      enabled: 1\n" +
            "      settings:\n" +
            "        CPU: x86_64\n" +
            "  userData: \n";
        const string ScriptMeta = "fileFormatVersion: 2\nguid: 5f737374db13fde43bfc9f5e7aeab1ac\nMonoImporter:\n  serializedVersion: 2\n";

        [Test]
        public void RewritesPluginMetasAndCopiesEverythingElse()
        {
            var binary = new byte[70000];
            new Random(1).NextBytes(binary);
            var entries = new List<(string, byte[])>
            {
                (PluginGuid + "/asset", binary),
                (PluginGuid + "/asset.meta", Ascii(PluginMeta)),
                (PluginGuid + "/pathname", Ascii("Assets/VLCUnity/Plugins/Windows/x86_64/LibVLCSharp.dll\n00")),
                (ScriptGuid + "/asset", Ascii("class A {}")),
                (ScriptGuid + "/asset.meta", Ascii(ScriptMeta)),
                (ScriptGuid + "/pathname", Ascii("Assets/VLCUnity/A.cs")),
            };

            var output = new MemoryStream();
            UnityPackageReport report = UnityPackageRewriter.MakeCompatible(new MemoryStream(Package(entries)), output);

            Assert.That(report.Converted, Is.EqualTo(new[] { "Assets/VLCUnity/Plugins/Windows/x86_64/LibVLCSharp.dll" }));
            Dictionary<string, byte[]> result = ReadPackage(output.ToArray());
            Assert.That(result.Keys, Is.EquivalentTo(new[]
            {
                PluginGuid + "/asset", PluginGuid + "/asset.meta", PluginGuid + "/pathname",
                ScriptGuid + "/asset", ScriptGuid + "/asset.meta", ScriptGuid + "/pathname",
            }));
            Assert.That(result[PluginGuid + "/asset"], Is.EqualTo(binary));
            Assert.That(result[ScriptGuid + "/asset.meta"], Is.EqualTo(Ascii(ScriptMeta)));

            PluginImporterMetaConverter.Convert(PluginMeta, out string expected);
            Assert.That(Encoding.ASCII.GetString(result[PluginGuid + "/asset.meta"]), Is.EqualTo(expected));
        }

        [Test]
        public void MakeCompatibleFilesVerifiesTheOutput()
        {
            string directory = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try
            {
                string input = Path.Combine(directory, "in.unitypackage");
                string output = Path.Combine(directory, "out.unitypackage");
                File.WriteAllBytes(input, Package(new List<(string, byte[])>
                {
                    (PluginGuid + "/asset.meta", Ascii(PluginMeta)),
                    (PluginGuid + "/pathname", Ascii("Assets/VLCUnity/Plugins/libvlc.dll")),
                }));

                Assert.That(UnityPackageRewriter.FindIncompatibleMetas(input), Is.EqualTo(new[] { "Assets/VLCUnity/Plugins/libvlc.dll" }));
                UnityPackageRewriter.MakeCompatible(input, output);
                Assert.That(UnityPackageRewriter.FindIncompatibleMetas(output), Is.Empty);
            }
            finally
            {
                Directory.Delete(directory, true);
            }
        }

        static byte[] Ascii(string text) => Encoding.ASCII.GetBytes(text);

        // Minimal ustar writer, laid out like Unity's exporter: a directory entry per guid.
        static byte[] Package(List<(string Name, byte[] Data)> entries)
        {
            var tar = new MemoryStream();
            var directories = new HashSet<string>();
            foreach (var (name, data) in entries)
            {
                string directory = name.Substring(0, name.IndexOf('/') + 1);
                if (directories.Add(directory))
                    WriteEntry(tar, directory, '5', new byte[0]);
                WriteEntry(tar, name, '0', data);
            }
            tar.Write(new byte[1024], 0, 1024);

            var package = new MemoryStream();
            using (var gzip = new GZipStream(package, CompressionMode.Compress, true))
                tar.WriteTo(gzip);
            return package.ToArray();
        }

        static void WriteEntry(Stream tar, string name, char type, byte[] data)
        {
            var header = new byte[512];
            Put(header, 0, name);
            Put(header, 100, "0000777");
            Put(header, 108, "0000000");
            Put(header, 116, "0000000");
            Put(header, 124, Convert.ToString(data.Length, 8).PadLeft(11, '0'));
            Put(header, 136, "00000000000");
            header[156] = (byte)type;
            Put(header, 257, "ustar");
            Put(header, 263, "00");
            for (int i = 148; i < 156; i++)
                header[i] = (byte)' ';
            int sum = 0;
            foreach (byte b in header)
                sum += b;
            Put(header, 148, Convert.ToString(sum, 8).PadLeft(6, '0'));
            header[154] = 0;
            tar.Write(header, 0, 512);
            tar.Write(data, 0, data.Length);
            int pad = (512 - data.Length % 512) % 512;
            tar.Write(new byte[pad], 0, pad);
        }

        static void Put(byte[] header, int offset, string value) => Encoding.ASCII.GetBytes(value).CopyTo(header, offset);

        // Reads regular files back and checks every header checksum.
        static Dictionary<string, byte[]> ReadPackage(byte[] package)
        {
            var files = new Dictionary<string, byte[]>();
            var tar = new MemoryStream();
            using (var gzip = new GZipStream(new MemoryStream(package), CompressionMode.Decompress))
                gzip.CopyTo(tar);
            byte[] bytes = tar.ToArray();

            for (int offset = 0; offset + 512 <= bytes.Length;)
            {
                var header = new byte[512];
                Array.Copy(bytes, offset, header, 0, 512);
                if (Array.TrueForAll(header, b => b == 0))
                    break;

                int stored = Convert.ToInt32(Encoding.ASCII.GetString(header, 148, 8).Trim('\0', ' '), 8);
                for (int i = 148; i < 156; i++)
                    header[i] = (byte)' ';
                int sum = 0;
                foreach (byte b in header)
                    sum += b;
                Assert.That(sum, Is.EqualTo(stored), "tar header checksum");

                string name = Encoding.ASCII.GetString(header, 0, 100).TrimEnd('\0');
                int size = Convert.ToInt32(Encoding.ASCII.GetString(header, 124, 12).Trim('\0', ' '), 8);
                if (header[156] == '0')
                {
                    var data = new byte[size];
                    Array.Copy(bytes, offset + 512, data, 0, size);
                    files[name] = data;
                }
                offset += 512 + (size + 511) / 512 * 512;
            }
            return files;
        }
    }
}
