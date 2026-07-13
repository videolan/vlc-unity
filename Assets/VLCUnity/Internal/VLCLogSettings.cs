using UnityEngine;
using UnityEngine.Events;

namespace LibVLCSharp
{
    public enum LogRotationMode { Standard, Size, Time }

    public enum LogTimeInterval { Hourly, Daily, Monthly }

    public class VLCLogSettings : ScriptableObject
    {
        public bool writeToUnityConsole = true;
        public bool writeToTextFile = false;

        [Tooltip("How to handle old log files.")]
        public LogRotationMode rotationMode = LogRotationMode.Standard;

        [Tooltip("When to start a new log file. Example: A new file every day.")]
        public LogTimeInterval timeInterval = LogTimeInterval.Daily;

        [Tooltip("Where to save the log file.")]
        public string logFilePath = "vlc_log.txt";

        [Tooltip("The maximum size of a single log file in megabytes.")]
        public int maxFileSizeMB = 5;

        [Tooltip("How many old log files to keep before deleting the oldest.")]
        public int maxRetainedFiles = 3;

        public UnityEvent<string> onLogGenerated;
    }
}
