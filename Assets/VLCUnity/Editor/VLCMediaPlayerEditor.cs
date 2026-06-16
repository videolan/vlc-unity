using UnityEditor;
using UnityEngine;

namespace LibVLCSharp
{
    [CustomEditor(typeof(VLCMediaPlayer))]
    public class VLCMediaPlayerEditor : Editor
    {
        private Texture2D _bannerTexture;

        private bool _showMedia = true;
        private bool _showAudio = true;
        private bool _showTexture = true;
        private bool _showEvents = true;
        private bool _showAdvanced = true;
        private bool _showDebug = true;

        private SerializedProperty _mediaPath;
        private SerializedProperty _mediaOptions;
        private SerializedProperty _playOnAwake;

        private SerializedProperty _configuration;
        private Editor _configEditor;

        private SerializedProperty _useUnityAudio;
        private SerializedProperty _targetAudioSource;

        private SerializedProperty _flipTextureX;
        private SerializedProperty _flipTextureY;

        private SerializedProperty _onPlayerStateChanged;

        private SerializedProperty _libVLCArguments;

        private SerializedProperty _logToConsole;

        private void OnEnable()
        {
            _bannerTexture = AssetDatabase.LoadAssetAtPath<Texture2D>("Assets/VLCUnity/Textures/VLCForUnityBanner.png");

            _mediaPath = serializedObject.FindProperty("mediaPath");
            _mediaOptions = serializedObject.FindProperty("mediaOptions");
            _playOnAwake = serializedObject.FindProperty("playOnAwake");

            _configuration = serializedObject.FindProperty("Configuration");

            _useUnityAudio = serializedObject.FindProperty("useUnityAudio");
            _targetAudioSource = serializedObject.FindProperty("targetAudioSource");

            _flipTextureX = serializedObject.FindProperty("flipTextureX");
            _flipTextureY = serializedObject.FindProperty("flipTextureY");

            _onPlayerStateChanged = serializedObject.FindProperty("OnPlayerStateChanged");

            _libVLCArguments = serializedObject.FindProperty("libVLCArguments");

            _logToConsole = serializedObject.FindProperty("logToConsole");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            if (_bannerTexture != null)
            {
                float aspect = (float)_bannerTexture.width / _bannerTexture.height;
                float targetWidth = EditorGUIUtility.currentViewWidth - 40f;
                float targetHeight = targetWidth / aspect;

                GUILayout.Space(5);
                GUILayout.Label(_bannerTexture, GUILayout.Width(targetWidth), GUILayout.Height(targetHeight));
                GUILayout.Space(5);
            }

            _showMedia = EditorGUILayout.Foldout(_showMedia, "Media Settings", true, EditorStyles.foldoutHeader);
            if (_showMedia)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_mediaPath);
                EditorGUILayout.PropertyField(_mediaOptions, true);
                EditorGUILayout.PropertyField(_playOnAwake);
                EditorGUI.indentLevel--;
            }
            GUILayout.Space(2);

            _showAudio = EditorGUILayout.BeginFoldoutHeaderGroup(_showAudio, "Audio Settings");
            if (_showAudio)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.HelpBox("Note: Enable this if you plan to capture audio using the Unity Recorder.", MessageType.Info);
                EditorGUILayout.PropertyField(_useUnityAudio, new GUIContent("Use Unity Audio"));
                if (_useUnityAudio.boolValue)
                    EditorGUILayout.PropertyField(_targetAudioSource);
                EditorGUI.indentLevel--;
            }
            EditorGUILayout.EndFoldoutHeaderGroup();
            GUILayout.Space(2);

            _showTexture = EditorGUILayout.BeginFoldoutHeaderGroup(_showTexture, "Texture Correction");
            if (_showTexture)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_flipTextureX);
                EditorGUILayout.PropertyField(_flipTextureY);
                EditorGUI.indentLevel--;
            }
            EditorGUILayout.EndFoldoutHeaderGroup();
            GUILayout.Space(2);

            _showEvents = EditorGUILayout.BeginFoldoutHeaderGroup(_showEvents, "Events");
            if (_showEvents)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_onPlayerStateChanged);
                EditorGUI.indentLevel--;
            }
            EditorGUILayout.EndFoldoutHeaderGroup();
            GUILayout.Space(2);

            _showAdvanced = EditorGUILayout.Foldout(_showAdvanced, "Advanced", true, EditorStyles.foldoutHeader);
            if (_showAdvanced)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_configuration);

                if (_configuration.objectReferenceValue != null)
                {
                    EditorGUI.indentLevel++;
                    EditorGUILayout.BeginVertical(EditorStyles.helpBox);

                    CreateCachedEditor(_configuration.objectReferenceValue, null, ref _configEditor);
                    _configEditor.OnInspectorGUI();

                    EditorGUILayout.EndVertical();
                    EditorGUI.indentLevel--;
                }

                EditorGUILayout.PropertyField(_libVLCArguments, true);
                EditorGUI.indentLevel--;
            }
            GUILayout.Space(2);

            _showDebug = EditorGUILayout.BeginFoldoutHeaderGroup(_showDebug, "Debug");
            if (_showDebug)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_logToConsole);
                EditorGUI.indentLevel--;
            }
            EditorGUILayout.EndFoldoutHeaderGroup();

            serializedObject.ApplyModifiedProperties();
        }
    }
}
