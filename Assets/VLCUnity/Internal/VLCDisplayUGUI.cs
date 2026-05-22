using UnityEngine;
using UnityEngine.UI;

namespace LibVLCSharp
{
    [RequireComponent(typeof(RawImage))]
    public class VLCDisplayUGUI : MonoBehaviour
    {
        public VLCMediaPlayer mediaPlayer;

        private RawImage _rawImage;

        private void Awake()
        {
            _rawImage = GetComponent<RawImage>();
        }

        private void OnEnable()
        {
            if (mediaPlayer != null)
                mediaPlayer.OnTextureResized += ApplyTexture;
        }

        private void OnDisable()
        {
            if (mediaPlayer != null)
                mediaPlayer.OnTextureResized -= ApplyTexture;

            ClearTexture();
        }

        private void ApplyTexture(RenderTexture texture)
        {
            _rawImage.texture = texture;
        }

        private void ClearTexture()
        {
            _rawImage.texture = null;
        }
    }
}
