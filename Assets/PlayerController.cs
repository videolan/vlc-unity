using UnityEngine;
using UnityEngine.UI;
using System;
using System.Collections;
using System.Runtime.InteropServices;


public class PlayerController : MonoBehaviour
{
    int seekTimeDelta = 2000; // In ms

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
    public static extern void stopVLC ();

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

    public void playPause ()
    {
	Debug.Log ("[VLC] Toggling Play Pause !");
	playPauseVLC ();
    }

    public void stop ()
    {
	Debug.Log ("[VLC] Stopping Player !");
	stopVLC ();
    }

    public void seekForward ()
    {
	Debug.Log ("[VLC] Seeking forward !");
	int pos = getTimeVLC ();
	setTimeVLC(pos + seekTimeDelta);
    }
    
    public void seekBackward ()
    {
	Debug.Log ("[VLC] Seeking backward !");
	int pos = getTimeVLC ();
	setTimeVLC(pos - seekTimeDelta);
    }
}
