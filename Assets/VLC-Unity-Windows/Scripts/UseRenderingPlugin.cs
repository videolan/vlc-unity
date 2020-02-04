using UnityEngine;
using System;
using System.Collections;
using LibVLCSharp.Shared;

public class UseRenderingPlugin : MonoBehaviour
{
    LibVLC _libVLC;
    MediaPlayer _mediaPlayer;
    const int seekTimeDelta = 2000;
    Texture2D tex = null;

    bool playing;
    void Awake()
    {
        Core.Initialize(Application.dataPath);

        _libVLC = new LibVLC("--no-osd","--verbose=2");

        _mediaPlayer = new MediaPlayer(_libVLC);

        StartCoroutine("CallPluginAtEndOfFrames");

        PlayPause();
    }

    public void SeekForward()
    {
        Debug.Log("[VLC] Seeking forward !");
        _mediaPlayer.Time += seekTimeDelta;
    }

    public void SeekBackward()
    {
        Debug.Log("[VLC] Seeking backward !");
        _mediaPlayer.Time -= seekTimeDelta;
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
        if (_mediaPlayer == null) return;
        if (_mediaPlayer.IsPlaying)
        {
            _mediaPlayer.Pause();
        }
        else
        {
            playing = true;
            if(_mediaPlayer.Media == null)
            {
                _mediaPlayer.Media = new Media(_libVLC, "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4", FromType.FromLocation);
            }

            _mediaPlayer.Play();
        }
    }

    public void Stop ()
    {
        Debug.Log ("[VLC] Stopping Player !");

        playing = false;
        _mediaPlayer?.Stop();
        GetComponent<Renderer>().material.mainTexture = null;
    }

    void Start()
    {
    }

    private IEnumerator CallPluginAtEndOfFrames()
    {
        while (true)
        {
            // Wait until all frame rendering is done
            yield return new WaitForEndOfFrame();

            // We may not receive video size the first time           
            if (GetComponent<Renderer>().material.mainTexture == null && playing)
            {
                // If received size is not null, it and scale the texture
                uint i_videoHeight = 0;
                uint i_videoWidth = 0;

                _mediaPlayer.Size(0, ref i_videoWidth, ref i_videoHeight);
                IntPtr texptr = _mediaPlayer.GetTexture(out bool updated);
                if (i_videoWidth != 0 && i_videoHeight != 0 && updated && texptr != IntPtr.Zero)
                {
                    Debug.Log("Creating texture with height " + i_videoHeight + " and width " + i_videoWidth);
                    tex = Texture2D.CreateExternalTexture((int)i_videoWidth,
                        (int)i_videoHeight,
                        TextureFormat.RGBA32,
                        false,
                        true,
                        texptr);
                    GetComponent<Renderer>().material.mainTexture = tex;
                }
            }
            else if (tex != null && playing)
            {
                IntPtr texptr = _mediaPlayer.GetTexture(out bool updated);
                if (updated)
                {
                    tex.UpdateExternalTexture(texptr);
                }
            }
        }
    }
}
