using UnityEngine;

namespace LibVLCSharp
{
    public class VLCVideoHoverController : MonoBehaviour
    {
        [SerializeField] private float maxInteractDistance = 4.5f;
        [SerializeField] private LayerMask screenLayer;
        [SerializeField] private float verticalDrop = 0.35f;
        [SerializeField] private float depthClearance = 0.05f;

        [SerializeField] private VLCVideoInfoPanel infoPanel;
        [SerializeField] private VLCCenterVideoDisplay centerDisplay;

        private Transform _cameraTransform;

        private void Awake()
        {
            _cameraTransform = Camera.main.transform;

            if (infoPanel.gameObject.activeSelf)
                infoPanel.gameObject.SetActive(false);
        }

        private void Update()
        {
            if (!Physics.Raycast(_cameraTransform.position, _cameraTransform.forward, out RaycastHit hit, maxInteractDistance, screenLayer))
            {
                HidePanel();
                return;
            }

            if (!hit.collider.TryGetComponent(out VLCVideoScreenData screenData) || screenData.mediaPlayer == null)
            {
                HidePanel();
                return;
            }

            bool isFocused = centerDisplay.CurrentPlayer == screenData.mediaPlayer;

            if (VLCInput.InteractButtonDown())
            {
                isFocused = !isFocused;

                if (isFocused)
                    centerDisplay.Focus(screenData.mediaPlayer);
                else
                    centerDisplay.Unfocus();
            }

            Vector3 tvFaceDirection = hit.normal;
            Vector3 tvBottomCenter = hit.collider.bounds.center;
            tvBottomCenter.y = hit.collider.bounds.min.y;

            Vector3 targetPosition = tvBottomCenter
                 - (Vector3.up * verticalDrop)
                 + (tvFaceDirection * depthClearance);

            Quaternion targetRotation = Quaternion.LookRotation(-tvFaceDirection);

            bool isPanelActive = infoPanel.gameObject.activeSelf;
            float smoothSpeed = Time.deltaTime * 12f;

            Vector3 nextPosition = isPanelActive
                ? Vector3.Lerp(infoPanel.transform.position, targetPosition, smoothSpeed)
                : targetPosition;

            Quaternion nextRotation = isPanelActive
                ? Quaternion.Slerp(infoPanel.transform.rotation, targetRotation, smoothSpeed)
                : targetRotation;

            infoPanel.transform.SetPositionAndRotation(nextPosition, nextRotation);

            infoPanel.ShowInfo(screenData.mediaPlayer, screenData.videoTitle, isFocused);
        }

        private void HidePanel()
        {
            if (infoPanel.gameObject.activeSelf)
                infoPanel.HideInfo();
        }
    }
}
