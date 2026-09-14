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
                titleText.text = title.ToUpper();
                UpdateDynamicInfo();
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

        private void UpdateDynamicInfo()
        {
            var texture = _currentPlayer.OutputTexture;
            resolutionText.text = texture != null ? $"{texture.width}x{texture.height}" : string.Empty;

            TimeSpan currentTime = TimeSpan.FromMilliseconds(_currentPlayer.Time);
            TimeSpan totalTime = TimeSpan.FromMilliseconds(_currentPlayer.Duration);
            string timeFormat = currentTime.TotalHours >= 1 || totalTime.TotalHours >= 1
                ? @"hh\:mm\:ss" : @"mm\:ss";

            timeText.text = $"{currentTime.ToString(timeFormat)} / {totalTime.ToString(timeFormat)}";
            statusText.text = _currentPlayer.CurrentState.ToString().ToUpper();
        }
    }
}
