using UnityEngine;
using UnityEngine.UI;
using System;
using System.Collections;
using System.Runtime.InteropServices;


public class PlayerController : MonoBehaviour
{
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
	[DllImport ("__Internal")]
#else
	[DllImport("VlcUnityWrapper")]
#endif
	public static extern void playPauseVLC();

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
	[DllImport ("__Internal")]
#else
	[DllImport("VlcUnityWrapper")]
#endif
	public static extern void stopVLC();

     public void playPause() {
	  Debug.Log("[VLC] Toggling Play Pause !");
	  playPauseVLC();
     }

     public void stop() {
	  Debug.Log("[VLC] Stopping Player !");
	  stopVLC();
     }
}
