using System;
using UnityEngine;
using UnityEngine.UI;

namespace LibVLCSharp
{
    [RequireComponent(typeof(CanvasGroup))]
    public class VLCVideoInfoPanel : MonoBehaviour
    {
        [SerializeField] private Text titleText;
        [SerializeField] private Text statusText;
        [SerializeField] private Text resolutionText;
        [SerializeField] private Text timeText;
        [SerializeField] private Text promptText;

        private CanvasGroup _canvasGroup;
        private VLCMediaPlayer _currentPlayer;
        private float _targetAlpha;
        private string _timeFormat = @"mm\:ss";

        private void Awake()
        {
            _canvasGroup = GetComponent<CanvasGroup>();
            _canvasGroup.alpha = 0f;
        }

        public void ShowInfo(VLCMediaPlayer player, string title, bool isCurrentlyFocused)
        {
            if (!gameObject.activeSelf)
                gameObject.SetActive(true);

            _targetAlpha = 1f;
            UpdatePrompt(isCurrentlyFocused);

            if (_currentPlayer != player)
            {
                _currentPlayer = player;
                UpdateStaticInfo(title);
            }
        }

        public void HideInfo()
        {
            _currentPlayer = null;
            _targetAlpha = 0f;
        }

        private void Update()
        {
            _canvasGroup.alpha = Mathf.Lerp(_canvasGroup.alpha, _targetAlpha, Time.deltaTime * 12f);

            if (_targetAlpha <= 0f && _canvasGroup.alpha < 0.01f)
            {
                _canvasGroup.alpha = 0f;
                gameObject.SetActive(false);
                return;
            }

            if (_currentPlayer == null || _canvasGroup.alpha < 0.01f)
                return;

            UpdateDynamicInfo();
        }

        private void UpdatePrompt(bool isFocused)
        {
            if (promptText == null)
                return;

            promptText.text = isFocused ? "LEFT CLICK TO UNFOCUS" : "LEFT CLICK TO FOCUS";
            promptText.color = isFocused ? new Color32(231, 76, 60, 255) : new Color32(46, 204, 113, 255);
        }

        private void UpdateStaticInfo(string title)
        {
            titleText.text = title.ToUpper();

            var tracks = _currentPlayer.Tracks(TrackType.Video);
            if (tracks != null && tracks.Count > 0)
            {
                var videoTrack = tracks[0].Data.Video;
                resolutionText.text = $"{videoTrack.Width}x{videoTrack.Height}";
            }

            _timeFormat = _currentPlayer.Duration >= 3600000 ? @"hh\:mm\:ss" : @"mm\:ss";
        }

        private void UpdateDynamicInfo()
        {
            TimeSpan currentTime = TimeSpan.FromMilliseconds(_currentPlayer.Time);
            TimeSpan totalTime = TimeSpan.FromMilliseconds(_currentPlayer.Duration);

            timeText.text = $"{currentTime.ToString(_timeFormat)} / {totalTime.ToString(_timeFormat)}";
            statusText.text = _currentPlayer.CurrentState.ToString().ToUpper();
        }
    }
}
