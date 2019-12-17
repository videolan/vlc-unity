using UnityEngine;
using System;
using System.Collections;
using LibVLCSharp.Shared;
using System.Runtime.InteropServices;

public class UseRenderingPlugin : MonoBehaviour
{
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
    private const string dllname = "__Internal";
#else
    private const string dllname = "VlcUnityWrapper";
#endif

    private LibVLC LibVLC { get; set; }
    private MediaPlayer MediaPlayer { get; set; }

    // Native plugin rendering events are only called if a plugin is used
    // by some script. This means we have to DllImport at least
    // one function in some active script.
    // For this example, we'll call into plugin's SetTimeFromUnity
    // function and pass the current time so the plugin can animate.

    // Menu to select video
    public GameObject menuVideoSelector;
    public RemoteTimeDisplayer rtd;

    public int seekTimeDelta = 2000;

    private Texture2D tex = null;

    void Awake()
    {
        SetPluginPath(Application.dataPath);
        InitLibVLC(); // slow
    }

    void InitLibVLC()
    {
        try
        {
            Core.Initialize($"{Application.dataPath}\\Plugins");
            LibVLC = new LibVLC("--verbose=2");
        }
        catch (Exception ex)
        {
            Debug.Log(ex);
        }
    }

    void OnDisable() {
        MediaPlayer?.Stop();
        MediaPlayer?.Dispose();
        MediaPlayer = null;

        LibVLC?.Dispose();
        LibVLC = null;
    }

    [DllImport("RenderingPlugin")]
    static extern void Print(string toPrint);

    [DllImport("RenderingPlugin")]
    static extern void SetPluginPath(string path);

    public void OnMenuClick (int index)
    {
        string movieURL;
        
        switch (index) {
        case 1:
            string text = UniClipboard.GetText ();
            Uri uri = new Uri (text);
            if ( uri.IsWellFormedOriginalString() )
                movieURL = text;
            else
                return;
            break;
        case 2:
            movieURL = Constants.Movie480p;
            break;
        case 3:
            movieURL = Constants.Movie1080p;
            break;
        case 4:
        default:
            movieURL = Constants.Movie2160p;
            break;
        }

        menuVideoSelector.SetActive (false);

        if (MediaPlayer == null)
        {
            MediaPlayer = new MediaPlayer(LibVLC);
        }

        var r = MediaPlayer.Play(new Media(LibVLC, "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4", FromType.FromLocation));
        //   MediaPlayer.Play(new Media(LibVLC, movieURL, FromType.FromLocation));

        Debug.Log(r ? "Play successful" : "Play NOT successful");
        StartCoroutine("CallPluginAtEndOfFrames");
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
        rtd.setPlaying (false);
        StopCoroutine ("CallPluginAtEndOfFrames");
        MediaPlayer?.Stop();
        tex = null;
        GetComponent<Renderer> ().material.mainTexture = null;
        menuVideoSelector.SetActive (true);
    }

    public void seekForward()
    {
        Debug.Log ("[VLC] Seeking forward !");
        MediaPlayer.Time += seekTimeDelta;
    }

    public void seekBackward()
    {
        Debug.Log ("[VLC] Seeking backward !");
        MediaPlayer.Time -= seekTimeDelta;
    }

    void Start()
    {
#if UNITY_WEBGL && !UNITY_EDITOR
        RegisterPlugin();
#endif
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
                IntPtr texptr = MediaPlayer.GetFrame(out bool updated);
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
                IntPtr texptr = MediaPlayer.GetFrame(out bool updated);
                if (updated)
                {
                    tex.UpdateExternalTexture(texptr);
                }
            }

            // Issue a plugin rendering event with arbitrary integer identifier.
            // TODO: Remove or implement
            //GL.IssuePluginEvent (GetRenderEventFunc (), 1);
        }
    }
}

public static class Constants
{
    public const string Movie480p = "https://streams.videolan.org/benchmark/23_ducks_take_off_V_MPEG4-ISO-ASP_8bits_858x480_30_000fps.mkv";
    public const string Movie1080p = "https://streams.videolan.org/benchmark/45_in_to_tree_V_MPEG4-ISO-AVC_8bits_1920x1080_25_000fps.mkv";
    public const string Movie2160p = "http://streams.videolan.org/benchmark/29_ducks_take_off_V_VP8_3860x2160_30_068fps.webm";
}