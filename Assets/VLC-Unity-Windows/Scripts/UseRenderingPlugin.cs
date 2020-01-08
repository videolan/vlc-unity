using UnityEngine;
using System;
using System.Collections;
using LibVLCSharp.Shared;

public class UseRenderingPlugin : MonoBehaviour
{
    LibVLC LibVLC { get; set; }
    MediaPlayer MediaPlayer { get; set; }
    int seekTimeDelta = 2000;
    Texture2D tex = null;

    void Awake()
    {
        Core.Initialize(Application.dataPath);

        LibVLC = new LibVLC("--verbose=2");

        MediaPlayer = new MediaPlayer(LibVLC);

        MediaPlayer.Play(new Media(LibVLC, "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4", FromType.FromLocation));

        StartCoroutine("CallPluginAtEndOfFrames");
    }

    public void seekForward()
    {
        Debug.Log("[VLC] Seeking forward !");
        MediaPlayer.Time += seekTimeDelta;
    }

    public void seekBackward()
    {
        Debug.Log("[VLC] Seeking backward !");
        MediaPlayer.Time -= seekTimeDelta;
    }

    void OnDisable() 
    {
        MediaPlayer?.Stop();
        MediaPlayer?.Dispose();
        MediaPlayer = null;

        LibVLC?.Dispose();
        LibVLC = null;
    }

    public void playPause()
    {
        Debug.Log ("[VLC] Toggling Play Pause !");
        if (MediaPlayer == null) return;
        if (MediaPlayer.IsPlaying)
            MediaPlayer.Pause();
        else MediaPlayer.Play();
    }

    public void stop ()
    {
        Debug.Log ("[VLC] Stopping Player !");
        StopCoroutine ("CallPluginAtEndOfFrames");
        MediaPlayer?.Stop();
        tex = null;
        GetComponent<Renderer> ().material.mainTexture = null;
    }

    void Start()
    {
        Vector3 scale = transform.localScale;
        scale.x = -scale.x;
        transform.localScale = scale;
    }

    private IEnumerator CallPluginAtEndOfFrames()
    {
        while (true)
        {
            // Wait until all frame rendering is done
            yield return new WaitForEndOfFrame();

            // We may not receive video size the first time           
            if (tex == null)
            {
                // If received size is not null, it and scale the texture
                uint i_videoHeight = 0;
                uint i_videoWidth = 0;

                MediaPlayer.Size(0, ref i_videoWidth, ref i_videoHeight);
                IntPtr texptr = MediaPlayer.GetTexture(out bool updated);
                if (i_videoWidth != 0 && i_videoHeight != 0 && updated && texptr != IntPtr.Zero)
                {
                    Debug.Log("Creating texture with height " + i_videoHeight + " and width " + i_videoWidth);
                    tex = Texture2D.CreateExternalTexture((int)i_videoWidth,
                        (int)i_videoHeight,
                        TextureFormat.RGBA32,
                        false,
                        true,
                        texptr);
                    tex.filterMode = FilterMode.Point;
                    tex.Apply();
                    GetComponent<Renderer>().material.mainTexture = tex;
                }
            }
            else if (tex != null)
            {
                IntPtr texptr = MediaPlayer.GetTexture(out bool updated);
                if (updated)
                {
                    tex.UpdateExternalTexture(texptr);
                }
            }
        }
    }
}

public static class Constants
{
    public const string Movie480p = "https://streams.videolan.org/benchmark/23_ducks_take_off_V_MPEG4-ISO-ASP_8bits_858x480_30_000fps.mkv";
    public const string Movie1080p = "https://streams.videolan.org/benchmark/45_in_to_tree_V_MPEG4-ISO-AVC_8bits_1920x1080_25_000fps.mkv";
    public const string Movie2160p = "http://streams.videolan.org/benchmark/29_ducks_take_off_V_VP8_3860x2160_30_068fps.webm";
}