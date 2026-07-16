using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Serialization;

namespace LibVLCSharp
{
    [RequireComponent(typeof(RawImage))]
    public class VLCDisplayUGUI : MonoBehaviour
    {
        [FormerlySerializedAs("mediaPlayer")]
        [SerializeField] private VLCVideoProviderBase _mediaPlayer;

        public VLCVideoProviderBase MediaPlayer
        {
            get => _mediaPlayer;
            set
            {
                if (_mediaPlayer == value)
                    return;

                if (isActiveAndEnabled)
                    Unbind();

                _mediaPlayer = value;

                if (isActiveAndEnabled)
                    Bind();
            }
        }

        private RawImage _rawImage;
        private float _baseAlpha = 1f;
        private bool _faded;
        private static readonly Rect UnflippedUVRect = new Rect(0, 0, 1, 1);
        private static readonly Rect VideoUVRect = new Rect(1, 1, -1, -1);

        private void Awake()
        {
            _rawImage = GetComponent<RawImage>();
        }

        private void OnEnable()
        {
            Bind();
        }

        private void OnDisable()
        {
            Unbind();
            ApplyTexture(null);
            ApplyOpacity(1f);
        }

        private void Bind()
        {
            if (_rawImage == null)
                return;

            if (_mediaPlayer != null)
            {
                _mediaPlayer.OnTextureResized += ApplyTexture;
                _mediaPlayer.OnOpacityChanged += ApplyOpacity;
            }

            ApplyTexture(_mediaPlayer != null ? _mediaPlayer.OutputTexture : null);
            ApplyOpacity(_mediaPlayer != null ? _mediaPlayer.Opacity : 1f);
        }

        private void Unbind()
        {
            if (_mediaPlayer != null)
            {
                _mediaPlayer.OnTextureResized -= ApplyTexture;
                _mediaPlayer.OnOpacityChanged -= ApplyOpacity;
            }
        }

        private void ApplyOpacity(float opacity)
        {
            if (_rawImage == null || (!_faded && opacity >= 1f))
                return;

            Color color = _rawImage.color;
            if (!_faded)
                _baseAlpha = color.a;

            color.a = _baseAlpha * opacity;
            _rawImage.color = color;
            _faded = opacity < 1f;
        }

        private void ApplyTexture(RenderTexture texture)
        {
            if (_rawImage == null)
                return;

            _rawImage.uvRect = texture != null ? VideoUVRect : UnflippedUVRect;
            _rawImage.texture = texture;
        }
    }
}
