using UnityEngine;

namespace LibVLCSharp
{
    [RequireComponent(typeof(MeshRenderer))]
    public class VLCScreenVisibilityManager : MonoBehaviour
    {
        [SerializeField] private VLCMediaPlayer mediaPlayer;

        private MeshRenderer _meshRenderer;

        private void Awake()
        {
            if (mediaPlayer == null)
            {
                Destroy(this);
                return;
            }

            _meshRenderer = GetComponent<MeshRenderer>();

            mediaPlayer.OnFrameGenerated += EnableScreen;
        }

        private void OnDestroy()
        {
            if (mediaPlayer != null)
                mediaPlayer.OnFrameGenerated -= EnableScreen;
        }

        private void EnableScreen()
        {
            _meshRenderer.enabled = true;

            mediaPlayer.OnFrameGenerated -= EnableScreen;

            Destroy(this);
        }
    }
}
