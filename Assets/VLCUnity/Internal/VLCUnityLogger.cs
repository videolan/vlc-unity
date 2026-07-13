using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

[assembly: System.Runtime.CompilerServices.InternalsVisibleTo("VLCUnity.Tests")]
namespace LibVLCSharp
{
    public static class VLCUnityLogger
    {
        /// <summary>
        /// Triggered whenever a native log is generated.
        /// Note: This executes on the native thread, not the Unity Main Thread.
        /// </summary>
        public static event Action<string> OnNativeLogReceived;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogCallback([MarshalAs(UnmanagedType.LPStr)] string message, uint hexColor);

        [DllImport(OnLoad.UnityPlugin, CallingConvention = CallingConvention.Cdecl)]
        private static extern void SetLogCallback(LogCallback callback);

        private static LogCallback _logCallback;
        internal static VLCLogSettings _settings;
        private static BlockingCollection<string> _fileQueue;
        private static SynchronizationContext _mainThreadContext;
        private static CancellationTokenSource _queueCancellationTokenSource;

        private static string _baseFilePath;
        private static string _fileDir;
        private static string _fileName;
        private static string _fileExt;
        private static string _cachedTimeFormat;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        internal static void Initialize()
        {
            _mainThreadContext = SynchronizationContext.Current;

            Application.quitting -= OnQuit;
            Application.quitting += OnQuit;

            if (_settings == null)
            {
                _settings = Resources.Load<VLCLogSettings>("VLCLogSettings");

                if (_settings == null)
                {
                    _settings = ScriptableObject.CreateInstance<VLCLogSettings>();
                    _settings.hideFlags = HideFlags.HideAndDontSave;
                }
            }

            _logCallback = HandleNativeLog;
            SetLogCallback(_logCallback);

            if (_settings.writeToTextFile && !string.IsNullOrEmpty(_settings.logFilePath))
            {
                _baseFilePath = _settings.logFilePath;

                if (!Path.IsPathRooted(_baseFilePath))
                    _baseFilePath = Path.Combine(Application.persistentDataPath, _baseFilePath);

                _fileDir = Path.GetDirectoryName(_baseFilePath);
                _fileName = Path.GetFileNameWithoutExtension(_baseFilePath);
                _fileExt = Path.GetExtension(_baseFilePath);

                _cachedTimeFormat = _settings.timeInterval switch
                {
                    LogTimeInterval.Hourly => "yyyy-MM-dd_HH",
                    LogTimeInterval.Monthly => "yyyy-MM",
                    _ => "yyyy-MM-dd"
                };

                InitializeFileRolling();

                _queueCancellationTokenSource?.Cancel();
                _queueCancellationTokenSource = new CancellationTokenSource();

                _fileQueue?.CompleteAdding();
                _fileQueue = new BlockingCollection<string>();

                Task.Run(() => ProcessFileQueue(_queueCancellationTokenSource.Token), _queueCancellationTokenSource.Token);
            }
        }

        internal static void OnQuit()
        {
            SetLogCallback(null);

            _fileQueue?.CompleteAdding();
        }

        private static void InitializeFileRolling()
        {
            if (!Directory.Exists(_fileDir))
                Directory.CreateDirectory(_fileDir);

            if (_settings.rotationMode == LogRotationMode.Standard && File.Exists(_baseFilePath))
            {
                string prevPath = Path.Combine(_fileDir, $"{_fileName}-prev{_fileExt}");

                if (File.Exists(prevPath))
                    File.Delete(prevPath);

                File.Move(_baseFilePath, prevPath);
            }
        }

        public static void HookLibVLC(LibVLC libVLC)
        {
            if (libVLC == null)
                return;
            libVLC.Log += OnLibVLCLog;
        }

        public static void UnhookLibVLC(LibVLC libVLC)
        {
            if (libVLC == null)
                return;
            libVLC.Log -= OnLibVLCLog;
        }

        private static void OnLibVLCLog(object s, LogEventArgs e)
        {
            try
            {
                RouteLog($"[LibVLC] {e.FormattedLog}");
            }
            catch (Exception ex)
            {
                Debug.LogException(ex);
            }
        }

        [AOT.MonoPInvokeCallback(typeof(LogCallback))]
        private static void HandleNativeLog(string message, uint hexColor)
        {
            if (string.IsNullOrEmpty(message))
                return;

            RouteLog(message, hexColor);
        }

        private static readonly SendOrPostCallback _onLogGeneratedCallback = DispatchLogToUnityEvent;

        private static void DispatchLogToUnityEvent(object state)
        {
            if (_settings != null && _settings.onLogGenerated != null)
                _settings.onLogGenerated.Invoke((string)state);
        }

        internal static void RouteLog(string message, uint hexColor = 0xD2D2D2FF)
        {
            OnNativeLogReceived?.Invoke(message);

            if (_settings == null)
                return;

            string consoleMessage = $"<color=#{hexColor:X6}>{message}</color>";

            if (_settings.writeToUnityConsole)
                Debug.Log(consoleMessage);

            if (_mainThreadContext != null)
                _mainThreadContext.Post(_onLogGeneratedCallback, consoleMessage);

            if (_settings.writeToTextFile && _fileQueue != null)
            {
                try
                {
                    _fileQueue.TryAdd(message);
                }
                catch (InvalidOperationException)
                {
                }
            }
        }

        private static void ProcessFileQueue(CancellationToken token)
        {
            List<string> batch = new();

            try
            {
                foreach (var message in _fileQueue.GetConsumingEnumerable(token))
                {
                    batch.Add(message);

                    while (_fileQueue.TryTake(out string nextMessage))
                        batch.Add(nextMessage);

                    WriteBatchToFile(batch);
                    batch.Clear();
                }
            }
            catch (OperationCanceledException)
            {
            }
        }

        private static void WriteBatchToFile(List<string> batch)
        {
            try
            {
                string targetPath = _baseFilePath;

                if (_settings.rotationMode == LogRotationMode.Time)
                {
                    string timeSuffix = DateTime.Now.ToString(_cachedTimeFormat);
                    targetPath = Path.Combine(_fileDir, $"{_fileName}_{timeSuffix}{_fileExt}");
                }
                else if (_settings.rotationMode == LogRotationMode.Size)
                {
                    if (File.Exists(targetPath))
                    {
                        long currentSize = new FileInfo(targetPath).Length;
                        long maxSize = _settings.maxFileSizeMB * 1024L * 1024L;

                        if (currentSize >= maxSize)
                            RollSizeBasedFiles();
                    }
                }

                File.AppendAllLines(targetPath, batch);
            }
            catch (IOException ex)
            {
                Debug.LogWarning($"Failed to write log batch: {ex.Message}");
            }
        }

        private static void RollSizeBasedFiles()
        {
            for (int i = _settings.maxRetainedFiles - 1; i >= 1; i--)
            {
                string oldPath = Path.Combine(_fileDir, $"{_fileName}_{i}{_fileExt}");
                string newPath = Path.Combine(_fileDir, $"{_fileName}_{i + 1}{_fileExt}");

                if (File.Exists(oldPath))
                {
                    if (File.Exists(newPath))
                        File.Delete(newPath);

                    File.Move(oldPath, newPath);
                }
            }

            string firstRolledPath = Path.Combine(_fileDir, $"{_fileName}_1{_fileExt}");

            if (File.Exists(_baseFilePath))
            {
                if (File.Exists(firstRolledPath))
                    File.Delete(firstRolledPath);

                File.Move(_baseFilePath, firstRolledPath);
            }
        }
    }
}
