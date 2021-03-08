using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace LibVLCSharp
{
    class OnLoad
    {
        [DllImport("VLCUnityPlugin", CallingConvention = CallingConvention.Cdecl, EntryPoint = "libvlc_unity_set_color_space")]
        static extern void SetColorSpace(UnityColorSpace colorSpace);

        enum UnityColorSpace
        {
            Gamma = 0,
            Linear = 1,
        }
        
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        static void OnBeforeSceneLoadRuntimeMethod()
        {
          //  Debug.Log("UnityEngine.QualitySettings.activeColorSpace: " + PlayerColorSpace);
            SetColorSpace(PlayerColorSpace);
        }
        static UnityColorSpace PlayerColorSpace => QualitySettings.activeColorSpace == 0 ? UnityColorSpace.Gamma : UnityColorSpace.Linear;
    }
}