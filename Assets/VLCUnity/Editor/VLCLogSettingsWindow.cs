using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace LibVLCSharp
{
    public sealed class VLCLogSettingsWindow : EditorWindow
    {
        private const string DefaultSettingsFolder = "Assets/Resources";
        private const string FallbackSettingsFolder = "Assets/VLCUnity/Resources";

        private VLCLogSettings _settings;
        private Editor _settingsEditor;

        public static void ShowWindow()
        {
            var window = GetWindow<VLCLogSettingsWindow>("VLC Logging");
            window.minSize = new Vector2(360f, 420f);
            window.Show();
            window.Focus();
        }

        private void OnEnable()
        {
            _settings = FindOrCreateSettings();
        }

        private void OnDisable()
        {
            if (_settingsEditor != null)
                DestroyImmediate(_settingsEditor);
        }

        private void OnGUI()
        {
            if (_settings == null)
                _settings = FindOrCreateSettings();

            if (_settings == null)
            {
                EditorGUILayout.HelpBox(
                    "VLC Unity could not create its logging settings asset. Check the Console for the asset path that blocked creation.",
                    MessageType.Error);

                if (GUILayout.Button("Try Again"))
                    _settings = FindOrCreateSettings();

                return;
            }

            Editor.CreateCachedEditor(_settings, typeof(VLCLogSettingsEditor), ref _settingsEditor);
            _settingsEditor.OnInspectorGUI();
        }

        private static VLCLogSettings FindOrCreateSettings()
        {
            string[] guids = AssetDatabase.FindAssets($"t:{nameof(VLCLogSettings)}");
            Array.Sort(guids, StringComparer.Ordinal);

            foreach (string guid in guids)
            {
                string path = AssetDatabase.GUIDToAssetPath(guid);
                if (!IsRuntimeLoadable(path))
                    continue;

                var existingSettings = AssetDatabase.LoadAssetAtPath<VLCLogSettings>(path);
                if (existingSettings != null)
                    return existingSettings;
            }

            foreach (string folder in new[] { DefaultSettingsFolder, FallbackSettingsFolder })
            {
                string path = $"{folder}/{VLCLogSettings.ResourceName}.asset";
                if (AssetDatabase.LoadMainAssetAtPath(path) != null)
                    continue;

                EnsureFolderExists(folder);

                var settings = CreateInstance<VLCLogSettings>();
                settings.name = VLCLogSettings.ResourceName;
                AssetDatabase.CreateAsset(settings, path);
                AssetDatabase.SaveAssets();
                Debug.Log($"Created VLC logging settings at {path}", settings);
                return settings;
            }

            Debug.LogError(
                $"Could not create {VLCLogSettings.ResourceName}.asset because both {DefaultSettingsFolder} and {FallbackSettingsFolder} contain an asset with that name.");
            return null;
        }

        private static bool IsRuntimeLoadable(string path)
        {
            if (!string.Equals(
                    Path.GetFileNameWithoutExtension(path),
                    VLCLogSettings.ResourceName,
                    StringComparison.Ordinal))
            {
                return false;
            }

            string directory = Path.GetDirectoryName(path)?.Replace('\\', '/');
            return directory != null &&
                directory.EndsWith("/Resources", StringComparison.Ordinal);
        }

        private static void EnsureFolderExists(string folderPath)
        {
            string[] parts = folderPath.Split('/');
            string parent = parts[0];

            for (int index = 1; index < parts.Length; index++)
            {
                string child = $"{parent}/{parts[index]}";
                if (!AssetDatabase.IsValidFolder(child))
                    AssetDatabase.CreateFolder(parent, parts[index]);

                parent = child;
            }
        }
    }
}
