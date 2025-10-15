using UnityEngine;
using System;
using LibVLCSharp;

/// this class serves as an example on how to configure playback in Unity with VLC for Unity using LibVLCSharp.
/// for libvlcsharp usage documentation, please visit https://code.videolan.org/videolan/LibVLCSharp/-/blob/master/docs/home.md
/// On Android, make sure you require Internet access in your manifest to be able to access internet-hosted videos in these demo scenes.
public class VLCMinimalPlayback : MonoBehaviour
{
    LibVLC _libVLC;
    MediaPlayer _mediaPlayer;
    const int seekTimeDelta = 5000;
    Texture2D tex = null;
    bool playing;
    
    void Awake()
    {
        Core.Initialize(Application.dataPath);

        _libVLC = new LibVLC(enableDebugLogs: true);

        Application.SetStackTraceLogType(LogType.Log, StackTraceLogType.None);
        //_libVLC.Log += (s, e) => UnityEngine.Debug.Log(e.FormattedLog); // enable this for logs in the editor

        PlayPause();
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

    public void PlayPause()
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
                // playing remote media
                _mediaPlayer.Media = new Media(new Uri("https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_1080p_stereo.avi"));
            }

            _mediaPlayer.Play();
        }
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
            tex = TextureHelper.CreateNativeTexture(ref _mediaPlayer, linear: true);
            GetComponent<Renderer>().material.mainTexture = tex;
        }
        else if (tex != null)
        {
            TextureHelper.UpdateTexture(tex, ref _mediaPlayer);
        }
    }
}
