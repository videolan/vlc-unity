using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.Runtime.InteropServices;

public class TestBinding : MonoBehaviour {
  
#if (UNITY_IPHONE || UNITY_WEBGL) && !UNITY_EDITOR
    [DllImport ("__Internal")]
#else	
    [DllImport ("vlcWrapper")]
#endif
    private static extern void playFilm();
  
    // Use this for initialization
    void Awake () {
      playFilm();
    }
}
