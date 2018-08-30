using UnityEngine;
using UnityEngine.UI;
using System;
using System.Collections;
using System.Runtime.InteropServices;


public class RemoteTimeDisplayer : MonoBehaviour
{
    public Text txt;

    string length;
    bool playing;


    public void
    setPlaying (bool state) {
	playing = state;
    }

    string
    formatMsToStr (int ms)
    {
	TimeSpan t = TimeSpan.FromMilliseconds (ms);
	string formattedTime = t.Minutes + " : " + t.Seconds;
	if (t.Hours > 0) 
	    formattedTime = t.Hours + "h " + formattedTime;

	return formattedTime;
    }

    void
    Start ()
    {
	// Get text object to update
	txt = GetComponent<Text> ();
	length = formatMsToStr (0);
    }

    void
    Update ()
    {
	if (!playing)
	    return;
  
	// We may not receive length the first time
	if (length == formatMsToStr (0)) { 
            // FIXME
	  //  length = formatMsToStr (UseRenderingPlugin.getLengthVLC ());
	}

        // Format actual time and concate it to length
        // FIXME
        //	string pos = formatMsToStr (UseRenderingPlugin.getTimeVLC ());
        // FIXME
        //txt.text = pos + "\n" + length;
    }
}
