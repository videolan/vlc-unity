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

        private void OnEnable()
        {
            Bind();
        }

        private void OnDisable()
        {
            Unbind();
        }

        public void Focus(VLCMediaPlayer player)
        {
            if (CurrentPlayer == player)
                return;

            Unbind();
            CurrentPlayer = player;

            visualRoot.SetActive(true);
            displayMesh.MediaPlayer = player;

            if (isActiveAndEnabled)
                Bind();
        }

        public void Unfocus()
        {
            Unbind();
            visualRoot.SetActive(false);
            displayMesh.MediaPlayer = null;
            CurrentPlayer = null;
        }

        private void Bind()
        {
            // Activating visualRoot in Focus can also invoke OnEnable.
            Unbind();
            if (CurrentPlayer == null)
                return;

            CurrentPlayer.OnTextureResized += UpdateScreenShape;
            UpdateScreenShape(CurrentPlayer.OutputTexture);
        }

        private void Unbind()
        {
            if (CurrentPlayer != null)
                CurrentPlayer.OnTextureResized -= UpdateScreenShape;
        }

        private void UpdateScreenShape(RenderTexture texture)
        {
            if (texture == null)
                return;

            float aspect = (float)texture.width / texture.height;
            displayMesh.transform.localScale = new Vector3(screenHeight * aspect, screenHeight, 1f);
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
