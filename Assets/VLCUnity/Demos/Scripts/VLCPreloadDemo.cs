using UnityEngine;
using UnityEngine.UI;
using LibVLCSharp;

public class VLCPreloadDemo : MonoBehaviour
{
    [SerializeField] private VLCMediaPlayer vlcPlayer;
    [SerializeField] private Button preloadButton;
    [SerializeField] private Button swapButton;
    [SerializeField] private Text statusText;

    [SerializeField] private string video1 = "https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_1080p_stereo.avi";
    [SerializeField] private string video2 = "https://download.blender.org/durian/movies/Sintel.2010.1080p.mkv";

    private string _currentlyPlaying;
    private string _nextToPreload;

    private float _targetPreloadPercentage = 0f;
    private float _currentDisplayPercentage = 0f;
    private bool _isPreloadVisuallyDone = true;

    private void OnEnable()
    {
        preloadButton.onClick.AddListener(OnPreloadButtonClicked);
        swapButton.onClick.AddListener(OnSwapButtonClicked);

        if (vlcPlayer != null)
        {
            vlcPlayer.OnPreloadPrepared += OnPreloadReady;
            vlcPlayer.OnPreloadFailed += OnPreloadError;
            vlcPlayer.OnPreloadBuffering += OnPreloadBufferingUpdate;
        }
    }

    private void OnDisable()
    {
        preloadButton.onClick.RemoveListener(OnPreloadButtonClicked);
        swapButton.onClick.RemoveListener(OnSwapButtonClicked);

        if (vlcPlayer != null)
        {
            vlcPlayer.OnPreloadPrepared -= OnPreloadReady;
            vlcPlayer.OnPreloadFailed -= OnPreloadError;
            vlcPlayer.OnPreloadBuffering -= OnPreloadBufferingUpdate;
        }
    }

    private async void Start()
    {
        if (vlcPlayer == null)
            return;

        _currentlyPlaying = video1;
        _nextToPreload = video2;

        statusText.text = "Status: Loading Video 1...";
        swapButton.interactable = false;

        await vlcPlayer.OpenAsync(_currentlyPlaying);

        statusText.text = "Status: Playing Video 1";
    }

    private void Update()
    {
        if (!_isPreloadVisuallyDone)
        {
            _currentDisplayPercentage = Mathf.MoveTowards(_currentDisplayPercentage, _targetPreloadPercentage, Time.deltaTime * 75f);

            if (_currentDisplayPercentage < 100f)
            {
                statusText.text = $"Status: Preloading... {_currentDisplayPercentage:0.0}%";
            }
            else if (vlcPlayer.CurrentPreloadState == VLCMediaPlayer.PreloadState.Prepared)
            {
                _isPreloadVisuallyDone = true;
                statusText.text = "Status: READY! Hit the Swap Button!";
                swapButton.interactable = true;
            }
        }
    }

    private void OnPreloadButtonClicked()
    {
        if (vlcPlayer.CurrentPreloadState != VLCMediaPlayer.PreloadState.None)
            return;

        _targetPreloadPercentage = 0f;
        _currentDisplayPercentage = 0f;
        _isPreloadVisuallyDone = false;

        preloadButton.interactable = false;
        statusText.text = "Status: Preloading... 0.0%";

        _ = vlcPlayer.PreloadAsync(_nextToPreload);
    }

    private void OnSwapButtonClicked()
    {
        if (vlcPlayer.CurrentPreloadState != VLCMediaPlayer.PreloadState.Prepared)
            return;

        swapButton.interactable = false;
        statusText.text = "Status: Swapped and Playing!";

        vlcPlayer.SwapAndPlayNext();

        (_currentlyPlaying, _nextToPreload) = (_nextToPreload, _currentlyPlaying);
        preloadButton.interactable = true;
    }

    private void OnPreloadBufferingUpdate(float cachePercent)
    {
        _targetPreloadPercentage = cachePercent;
    }

    private void OnPreloadReady(string path)
    {
        _targetPreloadPercentage = 100f;
    }

    private void OnPreloadError(string path)
    {
        _isPreloadVisuallyDone = true;
        statusText.text = "Status: Error preloading!";
        preloadButton.interactable = true;
    }
}
