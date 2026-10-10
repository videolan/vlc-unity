using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace Videolabs.VLCUnity.Packaging
{
    public sealed class UnityPackageReport
    {
        public readonly List<string> Converted = new List<string>();
        public int AlreadyCompatible;
    }

    // A .unitypackage is a gzipped tar with one "<guid>/" folder per asset, holding "asset",
    // "asset.meta" and "pathname". This rewrites the PluginImporter asset.meta entries to
    // serializedVersion 2 and copies every other byte of the archive unchanged.
    public static class UnityPackageRewriter
    {
        const int Block = 512;
        static readonly UTF8Encoding Utf8 = new UTF8Encoding(false);

        public static UnityPackageReport MakeCompatible(string inputPath, string outputPath)
        {
            string temp = outputPath + ".tmp";
            try
            {
                UnityPackageReport report;
                using (var input = File.OpenRead(inputPath))
                using (var output = File.Create(temp))
                    report = MakeCompatible(input, output);

                List<string> remaining = FindIncompatibleMetas(temp);
                if (remaining.Count > 0)
                {
                    throw new InvalidDataException("Package still contains PluginImporter metas that Unity 2022.3 cannot read: " +
                        string.Join(", ", remaining));
                }

                if (File.Exists(outputPath))
                    File.Delete(outputPath);
                File.Move(temp, outputPath);
                return report;
            }
            finally
            {
                if (File.Exists(temp))
                    File.Delete(temp);
            }
        }

        public static UnityPackageReport MakeCompatible(Stream packageIn, Stream packageOut)
        {
            var report = new UnityPackageReport();
            var convertedGuids = new List<string>();
            var pathnames = new Dictionary<string, string>();

            using (var tarIn = new GZipStream(packageIn, CompressionMode.Decompress, true))
            using (var tarOut = new GZipStream(packageOut, CompressionLevel.Optimal, true))
            {
                ForEachEntry(tarIn, tarOut, (name, data) =>
                {
                    if (name.EndsWith("/pathname", StringComparison.Ordinal))
                    {
                        pathnames[GuidOf(name)] = FirstLine(data);
                        return data;
                    }
                    if (!name.EndsWith("/asset.meta", StringComparison.Ordinal))
                        return data;

                    var result = PluginImporterMetaConverter.Convert(Decode(data, out bool bom), out string converted);
                    if (result == PluginImporterMetaConverter.Result.AlreadyCompatible)
                        report.AlreadyCompatible++;
                    if (result != PluginImporterMetaConverter.Result.Converted)
                        return data;

                    convertedGuids.Add(GuidOf(name));
                    return Encode(converted, bom);
                });
            }

            foreach (string guid in convertedGuids)
                report.Converted.Add(pathnames.TryGetValue(guid, out string path) ? path : guid);
            return report;
        }

        // Asset paths (or guids) of PluginImporter metas that Unity 2022.3 cannot read.
        public static List<string> FindIncompatibleMetas(string packagePath)
        {
            var incompatible = new List<string>();
            var pathnames = new Dictionary<string, string>();
            using (var file = File.OpenRead(packagePath))
            using (var tarIn = new GZipStream(file, CompressionMode.Decompress))
            {
                ForEachEntry(tarIn, null, (name, data) =>
                {
                    if (name.EndsWith("/pathname", StringComparison.Ordinal))
                        pathnames[GuidOf(name)] = FirstLine(data);
                    else if (name.EndsWith("/asset.meta", StringComparison.Ordinal) &&
                             PluginImporterMetaConverter.IsIncompatible(Decode(data, out _)))
                        incompatible.Add(GuidOf(name));
                    return data;
                });
            }
            return incompatible.ConvertAll(guid => pathnames.TryGetValue(guid, out string path) ? path : guid);
        }

        // Walks the tar. For regular "asset.meta" and "pathname" entries the callback gets the
        // data and returns the data to write; every other entry is streamed through untouched.
        // With a null output the archive is only read.
        static void ForEachEntry(Stream tarIn, Stream tarOut, Func<string, byte[], byte[]> onEntry)
        {
            var header = new byte[Block];
            string longName = null;
            bool paxSize = false;

            while (true)
            {
                if (!ReadBlock(tarIn, header))
                    throw new InvalidDataException("Truncated .unitypackage: missing end of archive.");

                if (IsZero(header))
                {
                    tarOut?.Write(header, 0, Block);
                    if (tarOut != null)
                        tarIn.CopyTo(tarOut);
                    return;
                }

                long size = ParseSize(header);
                long padded = (size + Block - 1) / Block * Block;
                char type = (char)header[156];

                if (type == 'L' || type == 'x' || type == 'g')
                {
                    byte[] extended = ReadExactly(tarIn, padded);
                    if (type == 'L')
                        longName = CString(extended, 0, (int)size);
                    else if (type == 'x')
                        ParsePax(extended, (int)size, ref longName, ref paxSize);
                    tarOut?.Write(header, 0, Block);
                    tarOut?.Write(extended, 0, extended.Length);
                    continue;
                }

                string name = longName ?? EntryName(header);
                longName = null;
                bool regular = type == '0' || type == '\0';
                bool inspect = regular &&
                    (name.EndsWith("/asset.meta", StringComparison.Ordinal) || name.EndsWith("/pathname", StringComparison.Ordinal));

                if (!inspect)
                {
                    paxSize = false;
                    tarOut?.Write(header, 0, Block);
                    Copy(tarIn, tarOut, padded);
                    continue;
                }

                byte[] data = ReadExactly(tarIn, size);
                Copy(tarIn, null, padded - size);
                byte[] written = onEntry(name, data);

                if (written != data)
                {
                    // A pax "size" record would override the ustar size we are about to fix.
                    if (paxSize)
                        throw new InvalidDataException($"Cannot rewrite '{name}': its size is stored in a pax header.");
                    WriteSize(header, written.Length);
                    WriteChecksum(header);
                }
                paxSize = false;

                if (tarOut != null)
                {
                    tarOut.Write(header, 0, Block);
                    tarOut.Write(written, 0, written.Length);
                    int pad = (int)((Block - written.Length % Block) % Block);
                    tarOut.Write(new byte[pad], 0, pad);
                }
            }
        }

        static string EntryName(byte[] header)
        {
            string name = CString(header, 0, 100);
            bool ustar = header[257] == 'u' && header[258] == 's' && header[259] == 't' && header[260] == 'a' && header[261] == 'r';
            string prefix = ustar ? CString(header, 345, 155) : "";
            return prefix.Length > 0 ? prefix + "/" + name : name;
        }

        static long ParseSize(byte[] header)
        {
            if ((header[124] & 0x80) != 0)
            {
                long value = 0;
                for (int i = 125; i < 136; i++)
                    value = (value << 8) | header[i];
                return value;
            }
            string octal = Encoding.ASCII.GetString(header, 124, 12).Trim('\0', ' ');
            return octal.Length == 0 ? 0 : System.Convert.ToInt64(octal, 8);
        }

        static void WriteSize(byte[] header, long size)
        {
            byte[] octal = Encoding.ASCII.GetBytes(System.Convert.ToString(size, 8).PadLeft(11, '0'));
            Array.Copy(octal, 0, header, 124, 11);
            header[135] = 0;
        }

        static void WriteChecksum(byte[] header)
        {
            for (int i = 148; i < 156; i++)
                header[i] = (byte)' ';
            long sum = 0;
            foreach (byte b in header)
                sum += b;
            byte[] octal = Encoding.ASCII.GetBytes(System.Convert.ToString(sum, 8).PadLeft(6, '0'));
            Array.Copy(octal, 0, header, 148, 6);
            header[154] = 0;
            header[155] = (byte)' ';
        }

        static void ParsePax(byte[] data, int size, ref string path, ref bool hasSize)
        {
            // Records are "<length> <key>=<value>\n".
            string records = Utf8.GetString(data, 0, size);
            int pos = 0;
            while (pos < records.Length)
            {
                int space = records.IndexOf(' ', pos);
                if (space < 0 || !int.TryParse(records.Substring(pos, space - pos), out int length) || length <= 0)
                    break;
                string record = records.Substring(space + 1, Math.Max(0, pos + length - space - 2));
                if (record.StartsWith("path=", StringComparison.Ordinal))
                    path = record.Substring(5);
                else if (record.StartsWith("size=", StringComparison.Ordinal))
                    hasSize = true;
                pos += length;
            }
        }

        static string GuidOf(string name) => name.Substring(0, name.IndexOf('/') < 0 ? name.Length : name.IndexOf('/'));

        static string FirstLine(byte[] data)
        {
            string text = Utf8.GetString(data);
            int newline = text.IndexOf('\n');
            return (newline < 0 ? text : text.Substring(0, newline)).TrimEnd('\r');
        }

        static string Decode(byte[] data, out bool bom)
        {
            bom = data.Length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
            return bom ? Utf8.GetString(data, 3, data.Length - 3) : Utf8.GetString(data);
        }

        static byte[] Encode(string text, bool bom)
        {
            byte[] body = Utf8.GetBytes(text);
            if (!bom)
                return body;
            var result = new byte[body.Length + 3];
            result[0] = 0xEF; result[1] = 0xBB; result[2] = 0xBF;
            Array.Copy(body, 0, result, 3, body.Length);
            return result;
        }

        static string CString(byte[] buffer, int offset, int count)
        {
            int end = offset;
            while (end < offset + count && buffer[end] != 0)
                end++;
            return Utf8.GetString(buffer, offset, end - offset);
        }

        static bool IsZero(byte[] block)
        {
            foreach (byte b in block)
            {
                if (b != 0)
                    return false;
            }
            return true;
        }

        static bool ReadBlock(Stream stream, byte[] block)
        {
            int read = 0;
            while (read < block.Length)
            {
                int n = stream.Read(block, read, block.Length - read);
                if (n == 0)
                    return false;
                read += n;
            }
            return true;
        }

        static byte[] ReadExactly(Stream stream, long count)
        {
            var buffer = new byte[count];
            int read = 0;
            while (read < count)
            {
                int n = stream.Read(buffer, read, (int)(count - read));
                if (n == 0)
                    throw new InvalidDataException("Truncated .unitypackage entry.");
                read += n;
            }
            return buffer;
        }

        static void Copy(Stream from, Stream to, long count)
        {
            var buffer = new byte[81920];
            while (count > 0)
            {
                int n = from.Read(buffer, 0, (int)Math.Min(buffer.Length, count));
                if (n == 0)
                    throw new InvalidDataException("Truncated .unitypackage entry.");
                to?.Write(buffer, 0, n);
                count -= n;
            }
        }
    }
}
