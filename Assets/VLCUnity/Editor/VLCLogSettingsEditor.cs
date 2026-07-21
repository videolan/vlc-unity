using System;
using System.IO;
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

            float previousLabelWidth = EditorGUIUtility.labelWidth;
            EditorGUIUtility.labelWidth = Mathf.Clamp(
                EditorGUIUtility.currentViewWidth * 0.55f,
                190f,
                240f);

            EditorGUILayout.LabelField("Sources", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                "Player and playlist activity is enabled on each component. The global sources below are independent: LibVLC covers media, networking and playback; VLC Unity Rendering covers the native graphics and texture integration.",
                MessageType.Info);

            var libVLCEngineLogsProp = serializedObject.FindProperty(nameof(VLCLogSettings.includeLibVLCEngineLogs));
            EditorGUILayout.PropertyField(
                libVLCEngineLogsProp,
                new GUIContent("LibVLC Engine Logs", libVLCEngineLogsProp.tooltip));

            var nativeRenderingLogsProp = serializedObject.FindProperty(nameof(VLCLogSettings.includeNativeRenderingLogs));
            EditorGUILayout.PropertyField(
                nativeRenderingLogsProp,
                new GUIContent("VLC Unity Rendering Logs", nativeRenderingLogsProp.tooltip));

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
            EditorGUIUtility.labelWidth = previousLabelWidth;

            if (fileLoggingProp.boolValue)
                DrawLogFileLocation((VLCLogSettings)target);
        }

        private static void DrawLogFileLocation(VLCLogSettings settings)
        {
            EditorGUILayout.Space(6f);
            EditorGUILayout.LabelField("Current Log File", EditorStyles.boldLabel);

            string resolvedPath;
            try
            {
                resolvedPath = VLCUnityLogger.ResolveCurrentLogFilePath(settings, DateTime.Now);
            }
            catch (Exception exception) when (
                exception is ArgumentException ||
                exception is IOException ||
                exception is NotSupportedException)
            {
                EditorGUILayout.HelpBox(
                    $"The configured log path is invalid: {exception.Message}",
                    MessageType.Error);
                return;
            }

            if (string.IsNullOrEmpty(resolvedPath))
            {
                EditorGUILayout.HelpBox("Enter a log file path to see its resolved location.", MessageType.Info);
                return;
            }

            EditorGUILayout.SelectableLabel(
                resolvedPath,
                EditorStyles.textField,
                GUILayout.Height(EditorGUIUtility.singleLineHeight));

            string directory = Path.GetDirectoryName(resolvedPath);
            bool directoryExists = !string.IsNullOrEmpty(directory) && Directory.Exists(directory);
            bool fileExists = File.Exists(resolvedPath);

            EditorGUILayout.BeginHorizontal();
            using (new EditorGUI.DisabledScope(!directoryExists))
            {
                if (GUILayout.Button("Open Log Folder"))
                    EditorUtility.RevealInFinder(directory);
            }

            using (new EditorGUI.DisabledScope(!fileExists))
            {
                if (GUILayout.Button("Open Current Log File"))
                    EditorUtility.OpenWithDefaultApp(resolvedPath);
            }
            EditorGUILayout.EndHorizontal();

            if (!directoryExists)
            {
                EditorGUILayout.HelpBox(
                    "The folder will be created when file logging initializes in Play Mode.",
                    MessageType.None);
            }
            else if (!fileExists)
            {
                EditorGUILayout.HelpBox(
                    "The current log file will appear after VLC emits its first enabled record.",
                    MessageType.None);
            }
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
                "This is not the active VLC logging settings asset. Use Configure Global Logging in a VLCMediaPlayer Inspector to open the asset used at runtime.",
                MessageType.Error);

            if (GUILayout.Button("Open Active Logging Settings"))
                VLCLogSettingsWindow.ShowWindow();

            EditorGUILayout.Space();
        }
    }
}
