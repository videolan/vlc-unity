using System;
using UnityEngine;

namespace LibVLCSharp
{
    public abstract class VLCVideoProviderBase : MonoBehaviour
    {
        public abstract event Action<RenderTexture> OnTextureResized;
        public abstract RenderTexture OutputTexture { get; protected set; }

        public event Action<float> OnOpacityChanged;

        public float Opacity { get; private set; } = 1f;

        public void SetOpacity(float opacity)
        {
            opacity = Mathf.Clamp01(opacity);
            if (opacity == Opacity)
                return;

            Opacity = opacity;
            OnOpacityChanged?.Invoke(opacity);
        }
    }
}
