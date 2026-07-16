using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Playables;
using UnityEngine.Timeline;

namespace LibVLCSharp
{
    /// <summary>Drives a VLCMediaPlayer from a Timeline in Play Mode.</summary>
    [TrackClipType(typeof(VLCTimelineClip))]
    [TrackBindingType(typeof(VLCMediaPlayer))]
    public class VLCTimelineTrack : TrackAsset
    {
        public override Playable CreateTrackMixer(PlayableGraph graph, GameObject go, int inputCount)
        {
            return ScriptPlayable<VLCTimelineMixerBehaviour>.Create(graph, inputCount);
        }
    }

    public class VLCTimelineMixerBehaviour : PlayableBehaviour
    {
        private const double JumpThreshold = 0.5;
        private const float ResyncCooldown = 2f;

        private VLCMediaPlayer _player;
        private string _playerPath;
        private string _openedPath;
        private bool _playing;
        private bool _wantPlaying;
        private float _rate = 1f;
        private double _previousTarget = double.NaN;
        private bool _buffering;
        private bool _mediaStarting;
        private bool _openedPaused;
        private bool _initialSyncPending;
        private float _settledSince = float.PositiveInfinity;

        public override void ProcessFrame(Playable playable, FrameData info, object playerData)
        {
            if (!Application.isPlaying)
                return;

            var player = playerData as VLCMediaPlayer;
            if (player != _player)
            {
                Release();
                Acquire(player);
            }

            if (_player == null || _player.MediaPlayer == null)
                return;

            // libvlc drops a seek or pause sent before newly opened media plays; Time leaves 0 once it does.
            if (_mediaStarting && !_openedPaused && _player.Time > 0)
                _mediaStarting = false;

            VLCTimelineBehaviour clip = null;
            Playable clipPlayable = Playable.Null;
            float weight = 0f;
            for (int i = 0; i < playable.GetInputCount(); i++)
            {
                float inputWeight = playable.GetInputWeight(i);
                if (inputWeight > weight)
                {
                    weight = inputWeight;
                    clipPlayable = playable.GetInput(i);
                    clip = ((ScriptPlayable<VLCTimelineBehaviour>)clipPlayable).GetBehaviour();
                }
            }

            if (clip == null)
            {
                SetPlaying(false);
                _player.SetOpacity(0f);
                _previousTarget = double.NaN;
                return;
            }

            // Volume is not blended: on Windows libvlc volume is shared by every player in the process.
            _player.SetOpacity(weight);

            double target = clipPlayable.GetTime();
            double speed = info.effectiveSpeed * clipPlayable.GetSpeed();

            // FrameData reports Playing when a paused director is evaluated (scrubbing), so ask the director.
            var director = playable.GetGraph().GetResolver() as PlayableDirector;
            bool directorPlaying = director == null ||
                (director.state == PlayState.Playing &&
                 !(director.extrapolationMode == DirectorWrapMode.Hold && director.time >= director.duration));
            bool wantsPlay = directorPlaying && info.effectivePlayState == PlayState.Playing && speed > 0;

            bool opened = EnsureMedia(clip.mediaPath, !wantsPlay);

            double duration = _player.Duration / 1000.0;
            bool pastEnd = false;
            if (duration > 0 && target >= duration)
            {
                if (_player.loop)
                    target %= duration;
                else
                    pastEnd = true;
            }

            bool shouldPlay = wantsPlay && !pastEnd;
            double expected = _previousTarget + info.deltaTime * speed;
            bool jumped = double.IsNaN(_previousTarget) || Math.Abs(target - expected) > JumpThreshold;
            _previousTarget = target;

            if (!opened && !_mediaStarting && !pastEnd)
            {
                bool nearEnd = duration > 0 && target > duration - JumpThreshold;
                if (_player.CurrentState == VLCState.Stopped && (jumped || shouldPlay) && !nearEnd)
                {
                    // A stopped player cannot seek, so reopen it.
                    Open(_openedPath, !shouldPlay);
                }
                else if (jumped)
                {
                    Seek(target);
                }
                else if (shouldPlay && !_buffering && _player.CurrentState == VLCState.Playing &&
                         (_initialSyncPending || Time.unscaledTime - _settledSince > ResyncCooldown))
                {
                    // The first check after opening skips the cooldown so the opening delay is caught up at once.
                    _initialSyncPending = false;
                    if (Math.Abs(_player.Time / 1000.0 - target) > clip.syncTolerance)
                        Seek(target);
                }
            }

            if (shouldPlay)
                SetRate((float)speed);
            SetPlaying(shouldPlay);
        }

        public override void OnBehaviourPause(Playable playable, FrameData info)
        {
            if (_player != null && _player.MediaPlayer != null)
                SetPlaying(false);
        }

        public override void OnPlayableDestroy(Playable playable)
        {
            Release();
        }

        private void Acquire(VLCMediaPlayer player)
        {
            _player = player;
            _openedPath = null;
            _playing = _wantPlaying = player != null && player.IsPlaying;
            _rate = 1f;
            _previousTarget = double.NaN;
            _buffering = false;
            _mediaStarting = false;
            _openedPaused = false;
            _initialSyncPending = false;
            _settledSince = float.PositiveInfinity;

            if (player == null)
                return;

            // Open() overwrites mediaPath, so keep the player's own path for clips that leave theirs empty.
            _playerPath = player.mediaPath;
            player.OnBuffering += OnBuffering;
            player.OnPlayerStateChanged.AddListener(OnStateChanged);
        }

        private void Release()
        {
            if (_player != null)
            {
                _player.OnBuffering -= OnBuffering;
                _player.OnPlayerStateChanged.RemoveListener(OnStateChanged);
                _player.mediaPath = _playerPath;

                if (_player.MediaPlayer != null)
                {
                    SetPlaying(false);
                    SetRate(1f);
                    _player.SetOpacity(1f);
                }
            }

            _player = null;
        }

        private void OnBuffering(float percent)
        {
            bool buffering = percent < 100f;
            if (_buffering && !buffering)
                _settledSince = Time.unscaledTime;
            _buffering = buffering;
        }

        private void OnStateChanged(VLCState state)
        {
            // Media opened paused never moves Time, so its Paused event marks it as started.
            if (_mediaStarting && ((_openedPaused && state == VLCState.Paused) || state == VLCState.Error))
            {
                _mediaStarting = false;
                ApplyPlaying();
            }
        }

        private bool EnsureMedia(string clipPath, bool paused)
        {
            string path = string.IsNullOrWhiteSpace(clipPath) ? _playerPath : clipPath;
            if (path == _openedPath || string.IsNullOrWhiteSpace(path))
                return false;

            Open(path, paused);
            return true;
        }

        private void Open(string path, bool paused)
        {
            var options = new List<string>(_player.mediaOptions ?? Array.Empty<string>());
            if (paused)
                options.Add(":start-paused");

            _openedPath = path;
            _player.Open(path, options.ToArray());
            _playing = !paused;
            _mediaStarting = true;
            _openedPaused = paused;
            _initialSyncPending = true;
            _settledSince = Time.unscaledTime;
        }

        private void Seek(double seconds)
        {
            _player.SetTime((long)Math.Round(seconds * 1000.0));
            _settledSince = Time.unscaledTime;
        }

        private void SetPlaying(bool playing)
        {
            _wantPlaying = playing;
            ApplyPlaying();
        }

        private void ApplyPlaying()
        {
            if (_playing == _wantPlaying || _player == null || _player.MediaPlayer == null)
                return;

            if (_mediaStarting)
            {
                // libvlc drops media paused before it plays, so reopen it paused instead.
                if (!_wantPlaying && !_openedPaused)
                    Open(_openedPath, true);
                return;
            }

            // Pause() toggles.
            if (_wantPlaying && (_player.CurrentState == VLCState.Stopped || _player.CurrentState == VLCState.NothingSpecial))
                _player.Play();
            else
                _player.MediaPlayer.SetPause(!_wantPlaying);

            _playing = _wantPlaying;
        }

        private void SetRate(float rate)
        {
            if (Math.Abs(rate - _rate) <= 0.001f)
                return;

            if (_player.MediaPlayer.SetRate(rate))
                _rate = rate;
        }
    }
}
