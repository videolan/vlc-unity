using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Videolabs.VLCUnity.Packaging
{
    // Unity 6 writes PluginImporter metas with serializedVersion 3, where platformData is a
    // map keyed by platform name. Unity 2022.3 and older only understand serializedVersion 2,
    // where platformData is a list of { first: { group: platform }, second: ... } pairs. They
    // do not report an error: the plugin silently ends up disabled on every platform (so
    // LibVLCSharp.dll is never referenced and the native plugins are never loaded or shipped).
    // Unity 6 still reads version 2, so packages must ship version 2 metas to work everywhere.
    public static class PluginImporterMetaConverter
    {
        public enum Result
        {
            NotPluginImporter,
            AlreadyCompatible,
            Converted,
        }

        // Version 3 platform key -> version 2 group, as written by Unity 2022.3.47f1.
        // Unknown platforms are rejected rather than guessed: a wrong group is read as
        // "disabled" by older editors without any warning.
        static readonly Dictionary<string, string> GroupByPlatform = new Dictionary<string, string>
        {
            { "Any", "Any" },
            { "Editor", "Editor" },
            { "Android", "Android" },
            { "CloudRendering", "LinuxHeadlessSimulation" },
            { "EmbeddedLinux", "EmbeddedLinux" },
            { "GameCoreScarlett", "GameCoreScarlett" },
            { "GameCoreXboxOne", "GameCoreXboxOne" },
            { "iOS", "iPhone" },
            { "Linux64", "Standalone" },
            { "OSXUniversal", "Standalone" },
            { "PS4", "PS4" },
            { "PS5", "PS5" },
            { "QNX", "QNX" },
            { "Switch", "Nintendo Switch" },
            { "tvOS", "tvOS" },
            { "VisionOS", "VisionOS" },
            { "WebGL", "WebGL" },
            { "Win", "Standalone" },
            { "Win64", "Standalone" },
            { "WindowsStoreApps", "Windows Store Apps" },
            { "XboxOne", "XboxOne" },
        };

        const string ImporterLine = "PluginImporter:";
        const string VersionPrefix = "  serializedVersion: ";
        const string PlatformDataLine = "  platformData:";

        public static Result Convert(string meta, out string converted)
        {
            converted = meta;
            string newline = meta.Contains("\r\n") ? "\r\n" : "\n";
            string[] lines = meta.Replace("\r\n", "\n").Split('\n');

            int importer = Array.IndexOf(lines, ImporterLine);
            if (importer < 0)
                return Result.NotPluginImporter;

            int end = importer + 1;
            while (end < lines.Length && Indent(lines[end]) > 0)
                end++;

            int version = FindLine(lines, importer + 1, end, l => l.StartsWith(VersionPrefix, StringComparison.Ordinal));
            if (version < 0)
                throw new InvalidDataException("PluginImporter meta has no serializedVersion.");
            string versionValue = lines[version].Substring(VersionPrefix.Length).Trim();
            if (versionValue == "2")
                return Result.AlreadyCompatible;
            if (versionValue != "3")
                throw new InvalidDataException($"Unsupported PluginImporter serializedVersion {versionValue}.");

            int platformData = FindLine(lines, importer + 1, end, l => l.StartsWith(PlatformDataLine, StringComparison.Ordinal));
            if (platformData < 0)
                throw new InvalidDataException("PluginImporter meta has no platformData.");

            var output = new List<string>(lines.Length + 64);
            for (int i = 0; i < platformData; i++)
                output.Add(i == version ? VersionPrefix + "2" : lines[i]);

            int next = platformData + 1;
            string inline = lines[platformData].Substring(PlatformDataLine.Length).Trim();
            if (inline == "{}" || inline == "[]")
            {
                output.Add(PlatformDataLine + " []");
            }
            else if (inline.Length > 0)
            {
                throw new InvalidDataException($"Unexpected inline platformData '{inline}'.");
            }
            else
            {
                var entries = ReadEntries(lines, ref next, end);
                if (entries.Count == 0)
                    output.Add(PlatformDataLine + " []");
                else
                    output.Add(PlatformDataLine);
                foreach (var entry in entries)
                {
                    output.Add("  - first:");
                    // Unity writes the "Any" pair with an empty value.
                    output.Add(entry.Platform == "Any" ? "      Any: " : $"      {entry.Group}: {entry.Platform}");
                    output.Add("    second:");
                    // Version 2 nests "second" at the same depth as version 3 nests the
                    // platform body, so the body lines are copied unchanged.
                    output.AddRange(entry.Body);
                }
            }

            for (int i = next; i < lines.Length; i++)
                output.Add(i == version ? VersionPrefix + "2" : lines[i]);

            converted = string.Join(newline, output);
            return Result.Converted;
        }

        // True when the text is a PluginImporter meta that Unity 2022.3 cannot read.
        public static bool IsIncompatible(string meta)
        {
            string[] lines = meta.Replace("\r\n", "\n").Split('\n');
            int importer = Array.IndexOf(lines, ImporterLine);
            if (importer < 0)
                return false;
            for (int i = importer + 1; i < lines.Length && Indent(lines[i]) > 0; i++)
            {
                if (lines[i].StartsWith(VersionPrefix, StringComparison.Ordinal))
                    return lines[i].Substring(VersionPrefix.Length).Trim() != "2";
            }
            return true;
        }

        struct Entry
        {
            public string Group;
            public string Platform;
            public List<string> Body;
        }

        static List<Entry> ReadEntries(string[] lines, ref int i, int end)
        {
            var entries = new List<Entry>();
            var seen = new HashSet<string>();
            while (i < end && Indent(lines[i]) >= 4)
            {
                string line = lines[i];
                if (Indent(line) != 4 || !line.EndsWith(":", StringComparison.Ordinal))
                    throw new InvalidDataException($"Unexpected platformData line '{line}'.");

                string platform = line.Substring(4, line.Length - 5);
                if (!GroupByPlatform.TryGetValue(platform, out string group))
                {
                    throw new InvalidDataException($"Unknown PluginImporter platform '{platform}'. " +
                        "Check how Unity 2022.3 writes it and add it to GroupByPlatform.");
                }
                if (!seen.Add(platform))
                    throw new InvalidDataException($"Duplicate PluginImporter platform '{platform}'.");

                var body = new List<string>();
                for (i++; i < end && Indent(lines[i]) >= 6; i++)
                    body.Add(lines[i]);

                entries.Add(new Entry { Group = group, Platform = platform, Body = body });
            }

            // Same order Unity 2022.3 writes: by group, then by platform, ordinal.
            entries.Sort((a, b) =>
            {
                int c = string.CompareOrdinal(a.Group, b.Group);
                return c != 0 ? c : string.CompareOrdinal(a.Platform, b.Platform);
            });
            return entries;
        }

        static int FindLine(string[] lines, int start, int end, Func<string, bool> match)
        {
            for (int i = start; i < end; i++)
            {
                if (Indent(lines[i]) == 2 && match(lines[i]))
                    return i;
            }
            return -1;
        }

        static int Indent(string line)
        {
            int n = 0;
            while (n < line.Length && line[n] == ' ')
                n++;
            return n == line.Length ? 0 : n;
        }
    }
}
