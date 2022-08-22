using UnityEngine;
using LibVLCSharp;
using System;

namespace LibVLCSharp
{
    // Unity flips textures differently depending on the graphics API used https://docs.unity3d.com/530/Documentation/Manual/SL-PlatformDifferences.html
    // We offer this helper in the meantime, until libvlc gets a new feature to be able to flip
    // textures internally in libvlc, after which this helper will not be needed anymore.
    public static class TextureHelper
    {
        public static void FlipTextures(Transform transform)
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            Vector3 rotationToAdd = new Vector3(0, 180, 0);
            transform.Rotate(rotationToAdd);
#endif
        }
    }    
}