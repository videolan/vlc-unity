using UnityEditor;
using UnityEngine;

namespace LibVLCSharp
{
    public class VLCLogSettingsWindow : EditorWindow
    {
        private const string SettingsPath = "Assets/VLCUnity/Resources/VLCLogSettings.asset";
        private const string SettingsFolder = "Assets/VLCUnity/Resources";
        private SerializedObject _serializedSettings;

        [MenuItem("VideoLAN/Logging Settings")]
        public static void ShowWindow()
        {
            var window = GetWindow<VLCLogSettingsWindow>("VLC Logging");
            window.minSize = new Vector2(300, 350);
            window.Show();
        }

        private void OnEnable()
        {
            RefreshSettings();
        }

        private void OnGUI()
        {
            if (_serializedSettings == null || _serializedSettings.targetObject == null)
                RefreshSettings();

            _serializedSettings.Update();

            EditorGUI.BeginChangeCheck();

            EditorGUILayout.Space();

            EditorGUILayout.PropertyField(_serializedSettings.FindProperty("writeToUnityConsole"));

            var fileLoggingProp = _serializedSettings.FindProperty("writeToTextFile");
            EditorGUILayout.PropertyField(fileLoggingProp);

            if (fileLoggingProp.boolValue)
            {
                EditorGUI.indentLevel++;

                var rotationProp = _serializedSettings.FindProperty("rotationMode");
                EditorGUILayout.PropertyField(rotationProp);

                EditorGUILayout.Space();
                EditorGUILayout.PropertyField(_serializedSettings.FindProperty("logFilePath"));

                switch (rotationProp.enumValueIndex)
                {
                    case (int)LogRotationMode.Standard:
                        EditorGUILayout.HelpBox("Keeps one active log file. The previous session is saved as a backup. Example: vlc_log-prev.txt", MessageType.Info);
                        break;

                    case (int)LogRotationMode.Size:
                        EditorGUILayout.HelpBox("Makes a new file when the size limit is reached. Keeps a set number of backups. Example: vlc_log_1.txt, vlc_log_2.txt", MessageType.Info);
                        EditorGUILayout.PropertyField(_serializedSettings.FindProperty("maxFileSizeMB"));
                        EditorGUILayout.PropertyField(_serializedSettings.FindProperty("maxRetainedFiles"));
                        break;

                    case (int)LogRotationMode.Time:
                        EditorGUILayout.HelpBox("Makes a brand new log file based on the time you choose. Example: vlc_log_2026-07-02.txt", MessageType.Info);
                        EditorGUILayout.PropertyField(_serializedSettings.FindProperty("timeInterval"));
                        break;
                }

                EditorGUI.indentLevel--;
            }

            EditorGUILayout.Space();
            EditorGUILayout.PropertyField(_serializedSettings.FindProperty("onLogGenerated"));

            if (EditorGUI.EndChangeCheck())
                _serializedSettings.ApplyModifiedProperties();
        }

        private void RefreshSettings()
        {
            _serializedSettings = new SerializedObject(GetOrCreateSettings());
        }

        private static VLCLogSettings GetOrCreateSettings()
        {
            var settings = AssetDatabase.LoadAssetAtPath<VLCLogSettings>(SettingsPath);

            if (settings == null)
            {
                var folders = SettingsFolder.Split('/');
                var parentFolder = folders[0];

                for (int i = 1; i < folders.Length; i++)
                {
                    var folderName = folders[i];
                    var folderPath = $"{parentFolder}/{folderName}";

                    if (!AssetDatabase.IsValidFolder(folderPath))
                        AssetDatabase.CreateFolder(parentFolder, folderName);

                    parentFolder = folderPath;
                }

                settings = CreateInstance<VLCLogSettings>();
                AssetDatabase.CreateAsset(settings, SettingsPath);
                AssetDatabase.SaveAssets();
            }

            return settings;
        }
    }
}
