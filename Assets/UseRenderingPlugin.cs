using UnityEngine;
using System;
using System.Collections;
using System.Runtime.InteropServices;


public class UseRenderingPlugin : MonoBehaviour
{
  // Native plugin rendering events are only called if a plugin is used
  // by some script. This means we have to DllImport at least
  // one function in some active script.
  // For this example, we'll call into plugin's SetTimeFromUnity
  // function and pass the current time so the plugin can animate.

  // Menu to select video
  public GameObject menuVideoSelector;
  public RemoteTimeDisplayer rtd;

  public int seekTimeDelta = 2000; // In ms

  // We'll also pass native pointer to a texture in Unity.
  // The plugin will fill texture data from native code.
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  private static extern void SetTextureFromUnity (System.IntPtr texture, int w, int h);

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  private static extern IntPtr GetRenderEventFunc ();
  
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  private static extern void launchVLC (string videoURL);
  
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  private static extern void stopVLC ();


#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  public static extern void playPauseVLC ();

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  public static extern int getLengthVLC ();

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  public static extern int getTimeVLC ();

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
  [DllImport ("__Internal")]
#else
  [DllImport ("VlcUnityWrapper")]
#endif
  public static extern void setTimeVLC (int pos);

#if UNITY_WEBGL && !UNITY_EDITOR
  [DllImport ("__Internal")]
  private static extern void RegisterPlugin ();
#endif

  public void
  OnMenuClick(int index)
  {
    string movieURL;

    switch (index)
      {
      case 1:
	movieURL = "file:///home/nimag42/VLC/VLC-Virtual-Cinema/Assets/Movies/AH.mp4";
	break;
      case 3:
	movieURL = "file:///home/nimag42/VLC/VLC-Virtual-Cinema/Assets/Movies/PERLIMPINPIN.mp4";
	break;
      case 2:
      default:
	movieURL = "file:///home/nimag42/VLC/VLC-Virtual-Cinema/Assets/Movies/Antman.mkv";
	break;
      }

    menuVideoSelector.SetActive(false);
    launchVLC (movieURL);
    rtd.setPlaying(true);
    StartCoroutine ("CallPluginAtEndOfFrames");
  }

  public void
  playPause ()
  {
    Debug.Log ("[VLC] Toggling Play Pause !");
    playPauseVLC ();
  }

  public void
  stop ()
  {
    Debug.Log ("[VLC] Stopping Player !");
    rtd.setPlaying(false);
    StopCoroutine("CallPluginAtEndOfFrames");
    stopVLC ();
    menuVideoSelector.SetActive(true);
  }

  public void
  seekForward ()
  {
    Debug.Log ("[VLC] Seeking forward !");
    int pos = getTimeVLC ();
    setTimeVLC(pos + seekTimeDelta);
  }

  public void
  seekBackward ()
  {
    Debug.Log ("[VLC] Seeking backward !");
    int pos = getTimeVLC ();
    setTimeVLC(Math.Max(0,pos - seekTimeDelta));
  }

  private void
  createTextureAndPassToPlugin ()
  {
    // Scale the texture as a screen 16/9 wide, so h = 9/16 ~= 0.5625
    transform.localScale = new Vector3 (-1.0f, 1.0f, -0.5625f);

    // Create a texture. TextureFormat should match the one you
    // choosed in liblvc_video_set_format.
    // RV32 matches BGRA32 if you use libvlc <= v2 (there's a bug that inverts chroma)
    // RV32 matches RGBA32 if you use libvlc >  v2
    Texture2D tex = new Texture2D (1920, 1080, TextureFormat.RGBA32, false);

    // Call Apply() so it's actually uploaded to the GPU
    tex.Apply();

    // Set texture onto our material
    GetComponent<Renderer>().material.mainTexture = tex;

    // Pass texture pointer to the plugin
    SetTextureFromUnity (tex.GetNativeTexturePtr(), tex.width, tex.height);
  }

  void
  Start ()
  {
#if UNITY_WEBGL && !UNITY_EDITOR
    RegisterPlugin();
#endif

    createTextureAndPassToPlugin ();
  }

  private IEnumerator
  CallPluginAtEndOfFrames()
  {
    while (true)
      {
	// Wait until all frame rendering is done
	yield return new WaitForEndOfFrame();

	// Issue a plugin event with arbitrary integer identifier.
	GL.IssuePluginEvent(GetRenderEventFunc(), 1);

	// // Code to automatically stop after 40sec, uncomment if needed
	// if(Time.timeSinceLevelLoad > 40.0f)
	//   {
	//     stopVLC();
	//     break;
	//   }

      }
  }
}
