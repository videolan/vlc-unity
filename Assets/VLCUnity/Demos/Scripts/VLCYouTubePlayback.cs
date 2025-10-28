using UnityEngine;
using System;
using System.Linq;
using System.Threading.Tasks;
using LibVLCSharp;

/// this class serves as an example on how to configure playback in Unity with VLC for Unity using LibVLCSharp.
/// for libvlcsharp usage documentation, please visit https://code.videolan.org/videolan/LibVLCSharp/-/blob/master/docs/home.md
/// On Android, make sure you require Internet access in your manifest to be able to access internet-hosted videos in these demo scenes.
public class VLCYouTubePlayback : MonoBehaviour
{
    LibVLC _libVLC;
    MediaPlayer _mediaPlayer;
    const int seekTimeDelta = 5000;
    Texture2D tex = null;
    bool playing;
    
    async void Awake()
    {
        Core.Initialize(Application.dataPath);

        _libVLC = new LibVLC(enableDebugLogs: true);

        Application.SetStackTraceLogType(LogType.Log, StackTraceLogType.None);
        //_libVLC.Log += (s, e) => UnityEngine.Debug.Log(e.FormattedLog); // enable this for logs in the editor

        await StartPlayback();
    }

    public void SeekForward()
    {
        Debug.Log("[VLC] Seeking forward !");
        _mediaPlayer.SetTime(_mediaPlayer.Time + seekTimeDelta);
    }

    public void SeekBackward()
    {
        Debug.Log("[VLC] Seeking backward !");
        _mediaPlayer.SetTime(_mediaPlayer.Time - seekTimeDelta);
    }

    void OnDisable() 
    {
        _mediaPlayer?.Stop();
        _mediaPlayer?.Dispose();
        _mediaPlayer = null;

        _libVLC?.Dispose();
        _libVLC = null;
    }

    public async Task StartPlayback()
    {
        Debug.Log ("[VLC] Toggling Play Pause !");
        if (_mediaPlayer == null)
        {
            _mediaPlayer = new MediaPlayer(_libVLC);
        }
        if (_mediaPlayer.IsPlaying)
        {
            _mediaPlayer.Pause();
        }
        else
        {
            playing = true;

            if(_mediaPlayer.Media == null)
            {
                // playing youtube video
                var youtubeLink = new Media(new Uri("https://www.youtube.com/watch?v=aqz-KE-bpKQ"));
                await youtubeLink.ParseAsync(_libVLC, MediaParseOptions.ParseNetwork);
                _mediaPlayer.Media = youtubeLink.SubItems.First();
            }

            _mediaPlayer.Play();
        }
    }

    public void PlayPause()
    {
        if(_mediaPlayer.IsPlaying)
            _mediaPlayer.Pause();
        else _mediaPlayer.Play();
    }

    public void Stop ()
    {
        Debug.Log ("[VLC] Stopping Player !");

        playing = false;
        _mediaPlayer?.Stop();
        
        // there is no need to dispose every time you stop, but you should do so when you're done using the mediaplayer and this is how:
        // _mediaPlayer?.Dispose(); 
        // _mediaPlayer = null;
        GetComponent<Renderer>().material.mainTexture = null;
        tex = null;
    }

    void Update()
    {
        if(!playing) return;

        if (tex == null)
        {
            // If received size is not null, it and scale the texture
            tex = TextureHelper.CreateNativeTexture(ref _mediaPlayer, linear: true);
            if (tex != null)
            {
                Debug.Log("Creating texture with height " + tex.height + " and width " + tex.width);
                GetComponent<Renderer>().material.mainTexture = tex;
            }
        }
        else if (tex != null)
        {
            TextureHelper.UpdateTexture(tex, ref _mediaPlayer);
        }
    }
}
