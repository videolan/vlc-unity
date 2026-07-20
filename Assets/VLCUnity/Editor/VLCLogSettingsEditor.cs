using UnityEditor;
using UnityEngine;

namespace LibVLCSharp
{
    [CustomEditor(typeof(VLCLogSettings))]
    public class VLCLogSettingsEditor : Editor
    {
        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            WarnIfUnloadable();

            EditorGUILayout.LabelField("Sources", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                "Component diagnostics are enabled on each VLCMediaPlayer or VLCPlaylistController. Enable the engine source below when deeper logs are needed.",
                MessageType.Info);

            var engineLogsProp = serializedObject.FindProperty(nameof(VLCLogSettings.captureEngineLogs));
            EditorGUILayout.PropertyField(engineLogsProp);

            if (engineLogsProp.boolValue)
            {
                EditorGUILayout.HelpBox(
                    "The VLC engine is very chatty, so this produces a lot of records. Enable it while diagnosing a problem, not in shipped builds.",
                    MessageType.Warning);
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Outputs", EditorStyles.boldLabel);

            EditorGUILayout.PropertyField(serializedObject.FindProperty(nameof(VLCLogSettings.writeToUnityConsole)));

            var fileLoggingProp = serializedObject.FindProperty(nameof(VLCLogSettings.writeToFile));
            EditorGUILayout.PropertyField(fileLoggingProp);

            if (fileLoggingProp.boolValue)
            {
                EditorGUI.indentLevel++;

                var rotationProp = serializedObject.FindProperty(nameof(VLCLogSettings.rotationMode));
                EditorGUILayout.PropertyField(rotationProp);

                EditorGUILayout.Space();
                EditorGUILayout.PropertyField(serializedObject.FindProperty(nameof(VLCLogSettings.logFilePath)));

                switch (rotationProp.enumValueIndex)
                {
                    case (int)LogRotationMode.PreviousSession:
                        EditorGUILayout.HelpBox("Keeps one active log file. The previous session is saved as a backup. Example: vlc_log-prev.txt", MessageType.Info);
                        break;

                    case (int)LogRotationMode.FileSize:
                        EditorGUILayout.HelpBox("Makes a new file when the size limit is reached. Keeps a set number of backups. Example: vlc_log_1.txt, vlc_log_2.txt", MessageType.Info);
                        EditorGUILayout.PropertyField(serializedObject.FindProperty(nameof(VLCLogSettings.maxFileSizeMB)));
                        EditorGUILayout.PropertyField(serializedObject.FindProperty(nameof(VLCLogSettings.maxRetainedFiles)));
                        break;

                    case (int)LogRotationMode.TimeInterval:
                        EditorGUILayout.HelpBox("Makes a brand new log file based on the time you choose. Example: vlc_log_2026-07-02.txt", MessageType.Info);
                        EditorGUILayout.PropertyField(serializedObject.FindProperty(nameof(VLCLogSettings.rotationInterval)));
                        break;
                }

                EditorGUI.indentLevel--;
            }

            EditorGUILayout.Space();
            EditorGUILayout.PropertyField(serializedObject.FindProperty(nameof(VLCLogSettings.onLogReceived)));

            serializedObject.ApplyModifiedProperties();
        }

        // The runtime loads these settings by name through Resources.Load, so an
        // asset that is misplaced or renamed is silently ignored. Say so here
        // rather than letting it look like the settings do not work.
        private void WarnIfUnloadable()
        {
            string path = AssetDatabase.GetAssetPath(target);

            if (string.IsNullOrEmpty(path))
                return;

            string directory = System.IO.Path.GetDirectoryName(path)?.Replace('\\', '/');
            bool directlyInResources = directory != null
                && directory.EndsWith("/Resources", System.StringComparison.Ordinal);
            bool correctName = System.IO.Path.GetFileNameWithoutExtension(path) == VLCLogSettings.ResourceName;

            if (directlyInResources && correctName)
                return;

            EditorGUILayout.HelpBox(
                $"These settings are ignored at runtime. The asset must be named {VLCLogSettings.ResourceName} and live directly inside a Resources folder, "
                + $"for example Assets/Resources/{VLCLogSettings.ResourceName}.asset.",
                MessageType.Error);
            EditorGUILayout.Space();
        }
    }
}
