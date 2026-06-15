using System;
using UnityEngine;
using UnityEngine.UI;

namespace LibVLCSharp
{
    [RequireComponent(typeof(CanvasGroup))]
    public class VLCStreamStats : MonoBehaviour
    {
        [SerializeField] private VLCMediaPlayer vlcPlayer;
        [SerializeField] private Text statusText;
        [SerializeField] private Text bufferingIndicator;

        [Header("Network")]
        [SerializeField] private Text inputBitrateText;
        [SerializeField] private UILineGraph inputBitrateGraph;
        [SerializeField] private Text demuxBitrateText;
        [SerializeField] private UILineGraph demuxBitrateGraph;
        [SerializeField] private Text totalDownloadedText;

        [Header("Playback")]
        [SerializeField] private Text timeText;
        [SerializeField] private Text resolutionText;
        [SerializeField] private Text codecText;

        [Header("Video")]
        [SerializeField] private Text videoDisplayedText;
        [SerializeField] private UILineGraph videoDisplayedGraph;
        [SerializeField] private Text videoDroppedText;
        [SerializeField] private UILineGraph videoDroppedGraph;
        [SerializeField] private Text videoHealthText;

        [Header("Audio")]
        [SerializeField] private Text audioPlayedText;
        [SerializeField] private UILineGraph audioPlayedGraph;
        [SerializeField] private Text audioDroppedText;
        [SerializeField] private UILineGraph audioDroppedGraph;
        [SerializeField] private Text audioHealthText;

        [Min(0.01f)]
        [SerializeField] private float pollRateSeconds = 0.25f;

        [SerializeField] private KeyCode toggleKey = KeyCode.F3;

        private CanvasGroup _canvasGroup;
        private bool _isVisible = true;

        private float _pollTimer;
        private float _lastRenderTime;
        private MediaStats _lastStats;

        private int _lastDisplayedVideo;
        private int _lastLostVideo;
        private int _lastPlayedAudio;
        private int _lastLostAudio;
        private VLCState _coreState = VLCState.Stopped;

        private void Awake()
        {
            _canvasGroup = GetComponent<CanvasGroup>();

            if (vlcPlayer == null)
            {
                var players = FindObjectsByType<VLCMediaPlayer>(FindObjectsSortMode.None);

                if (players.Length == 1)
                {
                    vlcPlayer = players[0];
                    Debug.Log("Auto-assigned the only VLCMediaPlayer in the scene.");
                }
                else if (players.Length > 1)
                {
                    Debug.LogWarning($"Found {players.Length} VLCMediaPlayers. Please link your target player manually in the inspector.");
                }
                else
                {
                    Debug.LogWarning("No VLCMediaPlayer found in the scene to track.");
                }
            }
        }

        private void Start()
        {
            if (vlcPlayer != null)
            {
                vlcPlayer.OnPlayerStateChanged.AddListener(OnPlayerStateChanged);
                vlcPlayer.OnBuffering += OnBufferingProgress;
            }
        }

        private void OnDestroy()
        {
            if (vlcPlayer != null)
            {
                vlcPlayer.OnPlayerStateChanged.RemoveListener(OnPlayerStateChanged);
                vlcPlayer.OnBuffering -= OnBufferingProgress;
            }
        }

        private void Update()
        {
            if (Input.GetKeyDown(toggleKey))
                _isVisible = !_isVisible;

            _canvasGroup.alpha = Mathf.Lerp(_canvasGroup.alpha, _isVisible ? 1f : 0f, Time.deltaTime * 10f);
            _canvasGroup.interactable = _isVisible;
            _canvasGroup.blocksRaycasts = _isVisible;

            if (!_isVisible || vlcPlayer == null || vlcPlayer.MediaPlayer?.Media == null)
                return;

            UpdateTime();

            _pollTimer += Time.deltaTime;
            if (_pollTimer >= pollRateSeconds)
            {
                UpdateStatsIfDirty();
                _pollTimer = 0f;
            }
        }

        private void UpdateStatsIfDirty()
        {
            MediaStats currentStats = vlcPlayer.MediaPlayer.Media.Statistics;

            if (currentStats.IsDirty(in _lastStats))
            {
                RenderStats(currentStats);
                _lastStats = currentStats;
            }
        }

        private void OnPlayerStateChanged(VLCState state)
        {
            if (state != VLCState.Buffering)
                _coreState = state;

            if (state == VLCState.Buffering && _coreState == VLCState.Playing)
                return;

            bufferingIndicator.text = "0%";

            statusText.text = state.ToString().ToUpper();

            if (state == VLCState.Playing)
            {
                statusText.color = new Color32(46, 204, 113, 255);
                UpdateMediaInfo();
            }
            else if (state == VLCState.Error)
            {
                statusText.color = new Color32(231, 76, 60, 255);
            }
            else
            {
                statusText.color = new Color32(245, 158, 11, 255);
            }
        }

        private void OnBufferingProgress(float percentage)
        {
            bufferingIndicator.text = $"{percentage:F0}%";
        }

        private void UpdateMediaInfo()
        {
            var tracks = vlcPlayer.Tracks(TrackType.Video);
            if (tracks == null || tracks.Count == 0)
                return;

            var videoTrack = tracks[0].Data.Video;
            uint fourcc = tracks[0].OriginalFourcc;
            string codecStr = vlcPlayer.MediaPlayer.Media.CodecDescription(TrackType.Video, fourcc);

            resolutionText.text = $"{videoTrack.Width}x{videoTrack.Height}";
            codecText.text = codecStr;
        }

        private void UpdateTime()
        {
            TimeSpan currentTime = TimeSpan.FromMilliseconds(vlcPlayer.Time);
            TimeSpan totalTime = TimeSpan.FromMilliseconds(vlcPlayer.Duration);

            string format = vlcPlayer.Duration >= 3600000 ? @"hh\:mm\:ss" : @"mm\:ss";
            timeText.text = $"{currentTime.ToString(format)} / {totalTime.ToString(format)}";
        }

        private void RenderStats(MediaStats stats)
        {
            float timeSinceLastRender = Mathf.Max(Time.unscaledTime - _lastRenderTime, 0.01f);
            _lastRenderTime = Time.unscaledTime;

            float inputBps = stats.InputBitrate * 8000f;
            float demuxBps = stats.DemuxBitrate * 8000f;

            inputBitrateText.text = FormatBitrate(inputBps);
            demuxBitrateText.text = FormatBitrate(demuxBps);
            inputBitrateGraph.AddPoint(inputBps);
            demuxBitrateGraph.AddPoint(demuxBps);

            totalDownloadedText.text = FormatBytes(stats.ReadBytes + stats.DemuxReadBytes);

            int currentDisplayedVideo = (int)stats.DisplayedPictures;
            int currentLostVideo = (int)stats.LostPictures;
            float displayedVideoRate = (currentDisplayedVideo - _lastDisplayedVideo) / timeSinceLastRender;
            float droppedVideoRate = (currentLostVideo - _lastLostVideo) / timeSinceLastRender;
            _lastDisplayedVideo = currentDisplayedVideo;
            _lastLostVideo = currentLostVideo;

            int totalVideo = currentDisplayedVideo + currentLostVideo;
            float vHealth = totalVideo > 0 ? (float)currentDisplayedVideo / totalVideo : 1f;

            videoDisplayedText.text = currentDisplayedVideo.ToString();
            videoDroppedText.text = currentLostVideo.ToString();
            videoHealthText.text = $"{vHealth * 100f:F1}%";
            videoHealthText.color = EvaluateHealthColor(vHealth);
            videoDisplayedGraph.AddPoint(displayedVideoRate);
            videoDroppedGraph.AddPoint(droppedVideoRate);

            int currentPlayedAudio = (int)stats.PlayedAudioBuffers;
            int currentLostAudio = (int)stats.LostAudioBuffers;
            float playedAudioRate = (currentPlayedAudio - _lastPlayedAudio) / timeSinceLastRender;
            float droppedAudioRate = (currentLostAudio - _lastLostAudio) / timeSinceLastRender;
            _lastPlayedAudio = currentPlayedAudio;
            _lastLostAudio = currentLostAudio;

            int totalAudio = currentPlayedAudio + currentLostAudio;
            float aHealth = totalAudio > 0 ? (float)currentPlayedAudio / totalAudio : 1f;

            audioPlayedText.text = currentPlayedAudio.ToString();
            audioDroppedText.text = currentLostAudio.ToString();
            audioHealthText.text = $"{aHealth * 100f:F1}%";
            audioHealthText.color = EvaluateHealthColor(aHealth);
            audioPlayedGraph.AddPoint(playedAudioRate);
            audioDroppedGraph.AddPoint(droppedAudioRate);
        }

        private Color EvaluateHealthColor(float percentage) => percentage switch
        {
            >= 0.98f => new Color32(46, 204, 113, 255),
            >= 0.85f => new Color32(245, 158, 11, 255),
            _ => new Color32(231, 76, 60, 255)
        };

        private string FormatBitrate(float bps) => bps switch
        {
            >= 1_000_000_000f => $"{bps / 1_000_000_000f:F2} Gbps",
            >= 1_000_000f => $"{bps / 1_000_000f:F2} Mbps",
            >= 1_000f => $"{bps / 1_000f:F2} Kbps",
            > 0f => $"{bps:F0} bps",
            _ => "0 bps"
        };

        private string FormatBytes(double bytes) => bytes switch
        {
            >= 1_099_511_627_776d => $"{bytes / 1_099_511_627_776d:F2} TB",
            >= 1_073_741_824d => $"{bytes / 1_073_741_824d:F2} GB",
            >= 1_048_576d => $"{bytes / 1_048_576d:F2} MB",
            >= 1_024d => $"{bytes / 1_024d:F2} KB",
            > 0d => $"{bytes:F0} B",
            _ => "0 B"
        };
    }

    public static class MediaStatsExtensions
    {
        public static bool IsDirty(this in MediaStats current, in MediaStats last)
        {
            return current.DisplayedPictures != last.DisplayedPictures ||
                   current.ReadBytes != last.ReadBytes ||
                   current.PlayedAudioBuffers != last.PlayedAudioBuffers ||
                   current.DemuxReadBytes != last.DemuxReadBytes ||
                   current.DemuxCorrupted != last.DemuxCorrupted ||
                   current.DemuxDiscontinuity != last.DemuxDiscontinuity ||
                   current.DecodedVideo != last.DecodedVideo ||
                   current.DecodedAudio != last.DecodedAudio ||
                   current.LatePictures != last.LatePictures ||
                   current.LostPictures != last.LostPictures ||
                   current.LostAudioBuffers != last.LostAudioBuffers ||
                   current.InputBitrate != last.InputBitrate ||
                   current.DemuxBitrate != last.DemuxBitrate;
        }
    }
}
