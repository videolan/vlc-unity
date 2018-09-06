using UnityEngine;
using System;
using System.Collections;
using LibVLCSharp.Shared;

/// <summary>
/// small snippet to enforce (only) 1 LibVLC instance (from which you can create multiple MediaPlayer objects)
/// </summary>
public static class LibVLCFactory
{
    static LibVLC _libVLC;
    static readonly object _lock = new object();

    public static LibVLC Get()
    {
        lock (_lock)
        {
            if (_libVLC == null)
            {
                _libVLC = new LibVLC(new[] { /*"--verbose=4" */ "--codec=mediacodec_ndk,all" , "--no-lua" });
            }
            return _libVLC;
        }
    }
}

public class UseRenderingPlugin2 : MonoBehaviour
{
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
    private const string dllname = "__Internal";
#else
    private const string dllname = "VlcUnityWrapper";
#endif
    public MediaPlayer MediaPlayer { get; private set; }

    // Native plugin rendering events are only called if a plugin is used
    // by some script. This means we have to DllImport at least
    // one function in some active script.
    // For this example, we'll call into plugin's SetTimeFromUnity
    // function and pass the current time so the plugin can animate.

    // Menu to select video
    public GameObject menuVideoSelector;
    public RemoteTimeDisplayer rtd;

    public int seekTimeDelta = 2000;

    private uint videoHeight = 0;
    private uint videoWidth = 0;

    private Texture2D tex = null;

    private LibVLC LibVLC;

#if UNITY_WEBGL && !UNITY_EDITOR
    [DllImport ("__Internal")]
    private static extern void RegisterPlugin ();
#endif

    static UseRenderingPlugin2()
    {
        Debug.Log("UseRenderingCalled");
        var currentPath = Environment.GetEnvironmentVariable("PATH",
             EnvironmentVariableTarget.Process);
        Debug.Log($"current path is: " + currentPath);
    }

    void OnEnable()
    {
        InitLibVLC();
    }

    void InitLibVLC()
    {
        try
        {
            LibVLC = LibVLCFactory.Get();
        }
        catch (Exception ex)
        {
            Debug.Log(ex);
        }
    }

    void Start()
    {
#if UNITY_WEBGL && !UNITY_EDITOR
        RegisterPlugin();
#endif
        Vector3 scale = transform.localScale;
        scale.x = -scale.x;
        scale.z = -scale.z;
        transform.localScale = scale;
    }

    public void OnMenuClick(int index)
    {
        Debug.Log("OnMenuClick2");
        string movieURL;

        switch (index)
        {
            case 1:
                string text = UniClipboard.GetText();
                Uri uri = new Uri(text);
                if (uri.IsWellFormedOriginalString())
                    movieURL = text;
                else
                    return;
                break;
            case 2:
                movieURL = "https://streams.videolan.org/benchmark/23_ducks_take_off_V_MPEG4-ISO-ASP_8bits_858x480_30_000fps.mkv";
                break;
            case 3:
                movieURL = "https://streams.videolan.org/benchmark/45_in_to_tree_V_MPEG4-ISO-AVC_8bits_1920x1080_25_000fps.mkv";
                break;
            case 4:
            default:
                movieURL = "https://streams.videolan.org/benchmark/35_ducks_take_off_V_VP9_3860x2160_25_000fps.mkv";
                //movieURL = "https://streams.videolan.org/benchmark/57_in_to_tree_V_MPEGH-ISO-HEVC_12bits_3860x2160_30_000fps.mkv";
                break;
        }
        
        menuVideoSelector.SetActive(false);
        videoWidth = 0;
        videoHeight = 0;

        if (MediaPlayer == null)
        {
            MediaPlayer = MediaPlayer.Create(LibVLC);
        }

        var r = MediaPlayer.Play(new Media(LibVLC, movieURL, Media.FromType.FromLocation));
        Debug.Log(r ? "Play successful" : "Play NOT successful");
        //    rtd.setPlaying (true);
        StartCoroutine("CallPluginAtEndOfFrames");
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
                MediaPlayer?.Size(0, ref i_videoWidth, ref i_videoHeight);
                bool updated;
                IntPtr texptr = MediaPlayer.GetFrame(out updated);

                if (i_videoWidth != 0 && i_videoHeight != 0 && updated)
                {
                    videoWidth = i_videoWidth;
                    videoHeight = i_videoHeight;
                    tex = Texture2D.CreateExternalTexture((int)videoWidth, (int)videoHeight, TextureFormat.RGBA32, false, true, texptr);
                    tex.filterMode = FilterMode.Point;
                    tex.Apply();
                    GetComponent<Renderer>().material.mainTexture = tex;
                }
            }
            else if (tex != null)
            {
                bool updated;
                IntPtr texptr = MediaPlayer.GetFrame(out updated);
                if (updated)
                {
                    //Debug.Log("Update texture");
                    tex.UpdateExternalTexture(texptr);
                }
                else
                {
                    //Debug.Log("texture not updated");
                }
            }

            // Issue a plugin rendering event with arbitrary integer identifier.
            //TODO: Remove or implement
            //GL.IssuePluginEvent (GetRenderEventFunc (), 1);
        }
    }
}