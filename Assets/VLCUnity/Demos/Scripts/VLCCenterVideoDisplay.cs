using UnityEngine;

namespace LibVLCSharp
{
    public class VLCCenterVideoDisplay : MonoBehaviour
    {
        [SerializeField] private VLCDisplayMesh displayMesh;
        [SerializeField] private GameObject visualRoot;
        [SerializeField] private float screenHeight = 1.5f;

        private Transform _cameraTransform;

        public VLCMediaPlayer CurrentPlayer { get; private set; }

        private void Awake()
        {
            _cameraTransform = Camera.main.transform;
        }

        public void Focus(VLCMediaPlayer player)
        {
            if (CurrentPlayer == player)
                return;

            CurrentPlayer = player;

            visualRoot.SetActive(true);
            displayMesh.MediaPlayer = player;

            if (player != null && player.OutputTexture != null)
            {
                float aspect = (float)player.OutputTexture.width / player.OutputTexture.height;
                displayMesh.transform.localScale = new Vector3(screenHeight * aspect, screenHeight, 1f);
            }
        }

        public void Unfocus()
        {
            visualRoot.SetActive(false);
            displayMesh.MediaPlayer = null;
            CurrentPlayer = null;
        }

        private void Update()
        {
            if (!visualRoot.activeSelf)
                return;

            if (VLCInput.CancelButtonDown())
            {
                Unfocus();
                return;
            }

            Vector3 lookDirection = _cameraTransform.position - visualRoot.transform.position;
            lookDirection.y = 0;

            if (lookDirection.sqrMagnitude > 0.001f)
                visualRoot.transform.rotation = Quaternion.LookRotation(-lookDirection);
        }
    }
}
