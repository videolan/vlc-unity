using UnityEngine;
using UnityEngine.UI;
using System;
using System.Collections;
using System.Runtime.InteropServices;


public class RemoteTimeDisplayer : MonoBehaviour
{
    public Text txt;
    private string length;

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
    [DllImport ("__Internal")]
#else
    [DllImport ("VlcUnityWrapper")]
#endif
    private static extern int getTimeVLC ();

#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
    [DllImport ("__Internal")]
#else
    [DllImport ("VlcUnityWrapper")]
#endif
    private static extern int getLengthVLC ();

    private string
    formatMsToStr (int ms)
    {
	TimeSpan t = TimeSpan.FromMilliseconds (ms);
	string formattedTime = t.Minutes + " : " + t.Seconds;
	if (t.Hours > 0) 
	    formattedTime = t.Hours + "h " + formattedTime;

	return formattedTime;
    }

    public void
    Start ()
    {
	// Get text object to update
	txt = GetComponent<Text> ();
	length = formatMsToStr (0);

	// Avoid duplication of code
	Update ();
    }

    void
    Update ()
    {
	// We may not receive length the first time
	if (length == formatMsToStr (0)) { 
	    length = formatMsToStr (getLengthVLC ());
	}
	// Format actual time and concate it to length
	string pos = formatMsToStr (getTimeVLC ());

	txt.text = pos + "\n" + length;
    }
}
