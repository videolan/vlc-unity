using UnityEngine;
using System;
using System.Collections;
using LibVLCSharp.Shared;

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

    private uint videoHeight = 0;
    private uint videoWidth = 0;

    private Texture2D tex = null;

#if UNITY_WEBGL && !UNITY_EDITOR
    [DllImport ("__Internal")]
    private static extern void RegisterPlugin ();
#endif

    static UseRenderingPlugin()
    {
        Debug.Log("UseRenderingCalled");
       var currentPath = Environment.GetEnvironmentVariable("PATH",
            EnvironmentVariableTarget.Process);
        Debug.Log ($"current path is: " + currentPath);
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

    void OnDisable() {
        MediaPlayer?.Stop();
        MediaPlayer?.Dispose();
        MediaPlayer = null;

        LibVLC?.Dispose();
        LibVLC = null;
    }

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
        videoWidth = 0;
        videoHeight = 0;

        if(MediaPlayer == null)
        {
            MediaPlayer = new MediaPlayer(LibVLC);
        }

        var r = MediaPlayer.Play(new Media(LibVLC, movieURL, Media.FromType.FromLocation));
        Debug.Log(r ? "Play successful" : "Play NOT successful");
    //    rtd.setPlaying (true);
        StartCoroutine ("CallPluginAtEndOfFrames");
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
        MediaPlayer?.Dispose();
        MediaPlayer = null;
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
        scale.z = -scale.z;
        transform.localScale = scale;
    }

    private IEnumerator CallPluginAtEndOfFrames()
    {
        while (true) {
            // Wait until all frame rendering is done
            yield return new WaitForEndOfFrame ();

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