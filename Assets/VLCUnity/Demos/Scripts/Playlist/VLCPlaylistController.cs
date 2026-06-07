using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Events;

namespace LibVLCSharp
{
    [Serializable]
    public struct PlaylistContext
    {
        public VLCPlaylistItem CurrentItem;
        public int CurrentIndex;
        public VLCState CurrentState;
        public bool IsCrossfading;
    }

    public class VLCPlaylistController : VLCVideoProviderBase
    {
        [Header("Configuration")]
        [SerializeField] private VLCPlaylistAsset playlist;
        [SerializeField] private bool playOnAwake = true;
        [SerializeField] private bool useUnityAudio = true;
        [SerializeField] private bool logToConsole = false;

        [Header("Events")]
        public UnityEvent<PlaylistContext> OnContextUpdated = new();
        public UnityEvent OnPlaylistEnded = new();

        public override event Action<RenderTexture> OnTextureResized;

        public VLCMediaPlayer ActivePlayer => _players[_activeIndex];
        public VLCMediaPlayer StandbyPlayer => _players[(_activeIndex + 1) % 2];
        public override RenderTexture OutputTexture { get; protected set; }

        private readonly VLCMediaPlayer[] _players = new VLCMediaPlayer[2];
        private List<int> _playOrder = new();

        private int _activeIndex;
        private int _currentIndex;
        private float _currentBlend;
        private VLCTransitionType _activeTransitionType;
        private float _activeTransitionDuration;

        private bool _isTransitioning;
        private bool _isUserStopped;

        private Material _blendMaterial;

        private void Awake()
        {
            InitializePlayers();

            Shader shader = Shader.Find("Hidden/VLC/Crossfade");
            if (shader != null)
                _blendMaterial = new Material(shader);
            else
                Log("Could not find crossfade shader!");
        }

        private void Start()
        {
            GeneratePlayOrder();

            if (playOnAwake)
                PlayCurrent();
        }

        private void Update()
        {
            HandleTimePollingForCrossfade();
            RenderOutput();
        }

        private void OnDestroy()
        {
            StopPlaylist();
            DestroyTextures();
        }

        public void PlayCurrent()
        {
            if (playlist == null || playlist.items.Count == 0)
                return;

            ResetStateFlags();

            int actualIndex = _playOrder[_currentIndex];
            VLCPlaylistItem item = playlist.items[actualIndex];

            if (string.IsNullOrWhiteSpace(item.path))
            {
                Log("Empty Path String. Advancing playlist.");
                AdvanceNext();
                return;
            }

            Log($"VLCPlaylistController PlayCurrent: {item.path}");

            string[] options = GetOptions(item);
            BindPlayerEvents(ActivePlayer);

            if (ActivePlayer.CurrentPreloadState != VLCMediaPlayer.PreloadState.None && ActivePlayer.PreloadedMediaPath == item.path)
                ActivePlayer.SwapAndPlayNext();
            else
                _ = ActivePlayer.OpenAsync(item.path, options);

            if (useUnityAudio)
            {
                ActivePlayer.targetAudioSource.volume = 1f;
                ActivePlayer.targetAudioSource.Play();
            }
            else
            {
                ActivePlayer.SetVolume(100);
            }

            BroadcastContext(VLCState.Playing);

            if (HasNextTrack())
                _ = PreloadNextTrackAsync();
        }

        public void AdvanceNext()
        {
            if (playlist == null || playlist.items.Count == 0)
                return;

            if (playlist.loopMode != VLCPlaylistLoopMode.Single)
            {
                if (!IncrementIndex())
                {
                    StopPlaylist();
                    return;
                }
            }

            int actualIndex = _playOrder[_currentIndex];
            VLCPlaylistItem targetItem = playlist.items[actualIndex];

            if ((StandbyPlayer.CurrentPreloadState == VLCMediaPlayer.PreloadState.Preparing || StandbyPlayer.CurrentPreloadState == VLCMediaPlayer.PreloadState.Prepared) && StandbyPlayer.PreloadedMediaPath == targetItem.path)
            {
                UnbindPlayerEvents(ActivePlayer);
                ActivePlayer.Stop();
                _activeIndex = (_activeIndex + 1) % 2;
            }

            PlayCurrent();
        }

        public void PlayPrevious()
        {
            if (playlist == null || playlist.items.Count == 0)
                return;

            if (_currentIndex > 0)
            {
                _currentIndex--;
                PlayCurrent();
            }
        }

        public void StopPlaylist()
        {
            Log("VLCPlaylistController StopPlaylist");

            _isUserStopped = true;
            ResetStateFlags();

            _players[0].Stop();
            _players[1].Stop();

            BroadcastContext(VLCState.Stopped);
        }

        private void InitializePlayers()
        {
            for (int i = 0; i < 2; i++)
            {
                GameObject playerObj = new($"VLC_InternalPlayer_{i}")
                {
                    hideFlags = HideFlags.HideInHierarchy
                };
                playerObj.transform.SetParent(transform);

                playerObj.SetActive(false);

                VLCMediaPlayer player = playerObj.AddComponent<VLCMediaPlayer>();
                player.useUnityAudio = useUnityAudio;
                player.playOnAwake = false;
                player.logToConsole = logToConsole;

                playerObj.SetActive(true);

                if (useUnityAudio)
                {
                    player.targetAudioSource.playOnAwake = false;
                    player.targetAudioSource.volume = i == 0 ? 1f : 0f;
                }
                else
                {
                    player.SetVolume(i == 0 ? 100 : 0);
                }

                _players[i] = player;
            }
        }

        private void GeneratePlayOrder()
        {
            if (playlist == null || playlist.items.Count == 0)
                return;

            _playOrder = Enumerable.Range(0, playlist.items.Count).ToList();

            if (playlist.shuffle)
            {
                for (int i = _playOrder.Count - 1; i > 0; i--)
                {
                    int j = UnityEngine.Random.Range(0, i + 1);
                    (_playOrder[i], _playOrder[j]) = (_playOrder[j], _playOrder[i]);
                }
            }
        }

        private void HandleTimePollingForCrossfade()
        {
            if (!ActivePlayer.IsPlaying || _isTransitioning || _isUserStopped || !HasNextTrack())
                return;

            int nextIndex = playlist.loopMode == VLCPlaylistLoopMode.Single ? _currentIndex : (_currentIndex + 1) % _playOrder.Count;
            VLCPlaylistItem nextItem = playlist.items[_playOrder[nextIndex]];

            VLCTransitionType transitionType = nextItem.overridePlaylistTransition ? nextItem.transitionType : playlist.defaultTransition;
            float transitionDuration = nextItem.overridePlaylistTransition ? nextItem.transitionDuration : playlist.defaultTransitionDuration;

            if (transitionType == VLCTransitionType.Cut || transitionDuration <= 0f)
                return;

            long totalDuration = ActivePlayer.Duration;
            long currentTime = ActivePlayer.Time;
            long crossfadeMs = (long)(transitionDuration * 1000);

            if (totalDuration > crossfadeMs && (totalDuration - currentTime) <= crossfadeMs)
            {
                _isTransitioning = true;
                _activeTransitionType = transitionType;
                _activeTransitionDuration = transitionDuration;

                BroadcastContext(ActivePlayer.CurrentState);
                StartCoroutine(ExecuteCrossfade());
            }
        }

        private void RenderOutput()
        {
            if (ActivePlayer == null || ActivePlayer.OutputTexture == null)
                return;

            uint width = (uint)ActivePlayer.OutputTexture.width;
            uint height = (uint)ActivePlayer.OutputTexture.height;

            ResizeOutputTextures(width, height);

            if (_isTransitioning && StandbyPlayer != null && StandbyPlayer.OutputTexture != null)
            {
                _blendMaterial.SetTexture("_SecondaryTex", StandbyPlayer.OutputTexture);
                _blendMaterial.SetFloat("_Blend", _currentBlend);
                _blendMaterial.SetInt("_TransitionMode", (int)_activeTransitionType);

                if (_activeTransitionType == VLCTransitionType.LumaDissolve && playlist != null && playlist.transitionNoiseMap != null)
                    _blendMaterial.SetTexture("_NoiseTex", playlist.transitionNoiseMap);

                Graphics.Blit(ActivePlayer.OutputTexture, OutputTexture, _blendMaterial);
            }
            else
            {
                Graphics.Blit(ActivePlayer.OutputTexture, OutputTexture);
            }
        }

        private void ResizeOutputTextures(uint px, uint py)
        {
            if (px == 0 || py == 0 || (OutputTexture != null && OutputTexture.width == px && OutputTexture.height == py))
                return;

            DestroyTextures();

            OutputTexture = new RenderTexture((int)px, (int)py, 0, RenderTextureFormat.ARGB32);
            OutputTexture.Create();

            OnTextureResized?.Invoke(OutputTexture);
        }

        private void DestroyTextures()
        {
            if (OutputTexture != null)
            {
                if (RenderTexture.active == OutputTexture)
                    RenderTexture.active = null;

                OutputTexture.Release();
                DestroyImmediate(OutputTexture);
                OutputTexture = null;
            }
        }

        private string[] GetOptions(VLCPlaylistItem item)
        {
            return item.options ?? Array.Empty<string>();
        }

        private async Task PreloadNextTrackAsync()
        {
            int nextIndex = playlist.loopMode == VLCPlaylistLoopMode.Single ? _currentIndex : (_currentIndex + 1) % _playOrder.Count;
            VLCPlaylistItem nextItem = playlist.items[_playOrder[nextIndex]];

            if (string.IsNullOrWhiteSpace(nextItem.path))
                return;

            string[] options = GetOptions(nextItem);

            try
            {
                await StandbyPlayer.PreloadAsync(nextItem.path, options);
            }
            catch (Exception e)
            {
                Log($"Preload failed: {e.Message}");
            }
        }

        private IEnumerator ExecuteCrossfade()
        {
            if (StandbyPlayer.CurrentPreloadState != VLCMediaPlayer.PreloadState.None)
            {
                StandbyPlayer.SwapAndPlayNext();
            }
            else
            {
                int nextIndex = playlist.loopMode == VLCPlaylistLoopMode.Single ? _currentIndex : (_currentIndex + 1) % _playOrder.Count;
                VLCPlaylistItem nextItem = playlist.items[_playOrder[nextIndex]];
                _ = StandbyPlayer.OpenAsync(nextItem.path, GetOptions(nextItem));
            }

            yield return new WaitUntil(() => StandbyPlayer.OutputTexture != null || _isUserStopped);

            if (_isUserStopped)
                yield break;

            if (useUnityAudio)
                StandbyPlayer.targetAudioSource.Play();

            float elapsed = 0f;

            while (elapsed < _activeTransitionDuration && !_isUserStopped)
            {
                elapsed += Time.deltaTime;
                float t = Mathf.Clamp01(elapsed / _activeTransitionDuration);
                _currentBlend = t;

                if (useUnityAudio)
                {
                    ActivePlayer.targetAudioSource.volume = Mathf.Lerp(1f, 0f, t);
                    StandbyPlayer.targetAudioSource.volume = Mathf.Lerp(0f, 1f, t);
                }
                else
                {
                    ActivePlayer.SetVolume((int)Mathf.Lerp(100, 0, t));
                    StandbyPlayer.SetVolume((int)Mathf.Lerp(0, 100, t));
                }

                yield return null;
            }

            if (_isUserStopped)
                yield break;

            FinalizeTransition();
        }

        private void FinalizeTransition()
        {
            UnbindPlayerEvents(ActivePlayer);
            ActivePlayer.Stop();

            _activeIndex = (_activeIndex + 1) % 2;
            _isTransitioning = false;
            _currentBlend = 0f;

            if (playlist.loopMode != VLCPlaylistLoopMode.Single)
                IncrementIndex();

            BindPlayerEvents(ActivePlayer);

            BroadcastContext(VLCState.Playing);

            if (HasNextTrack())
                _ = PreloadNextTrackAsync();
        }

        private void BindPlayerEvents(VLCMediaPlayer player)
        {
            player.OnPlayerStateChanged.RemoveListener(OnPlayerStateChanged);
            player.OnPlayerStateChanged.AddListener(OnPlayerStateChanged);
        }

        private void UnbindPlayerEvents(VLCMediaPlayer player)
        {
            player.OnPlayerStateChanged.RemoveListener(OnPlayerStateChanged);
        }

        private void OnPlayerStateChanged(VLCState state)
        {
            if (!_isTransitioning)
                BroadcastContext(state);

            if (state == VLCState.Stopped && !_isUserStopped && !_isTransitioning)
            {
                AdvanceNext();
            }
            else if (state == VLCState.Error)
            {
                Log("Track error occurred. Advancing playlist.");
                AdvanceNext();
            }
        }

        private bool HasNextTrack()
        {
            return _currentIndex < _playOrder.Count - 1 || playlist.loopMode == VLCPlaylistLoopMode.All || playlist.loopMode == VLCPlaylistLoopMode.Single;
        }

        private bool IncrementIndex()
        {
            if (_currentIndex < _playOrder.Count - 1)
            {
                _currentIndex++;
                return true;
            }

            if (playlist.loopMode == VLCPlaylistLoopMode.All)
            {
                GeneratePlayOrder();
                _currentIndex = 0;
                return true;
            }

            OnPlaylistEnded?.Invoke();
            return false;
        }

        private void ResetStateFlags()
        {
            StopAllCoroutines();

            if(useUnityAudio)
            {
                _players[0].targetAudioSource.Stop();
                _players[1].targetAudioSource.Stop();
            }

            UnbindPlayerEvents(_players[0]);
            UnbindPlayerEvents(_players[1]);

            _isTransitioning = false;
            _currentBlend = 0f;
            _isUserStopped = false;
        }

        private void Log(object message)
        {
            if (logToConsole)
                Debug.Log(message);
        }

        private void BroadcastContext(VLCState state)
        {
            if (playlist == null || playlist.items.Count == 0)
                return;

            int actualIndex = _playOrder[_currentIndex];

            OnContextUpdated?.Invoke(new PlaylistContext
            {
                CurrentItem = playlist.items[actualIndex],
                CurrentIndex = actualIndex,
                CurrentState = state,
                IsCrossfading = _isTransitioning
            });
        }
    }
}
