using System.Collections;
using UnityEngine;
using UnityEngine.Playables;

namespace LibVLCSharp
{
    public class VLCBranchingDemoController : MonoBehaviour
    {
        public PlayableDirector director;
        public double waterfallStartTime = 15.0;
        public double mountainStartTime = 30.0;

        public CanvasGroup branchingUI;
        public float fadeDuration = 1.0f;

        private Coroutine _fadeCoroutine;

        public void PauseForBranching()
        {
            if (director != null)
                director.Pause();

            if (branchingUI != null)
                FadeUI(1f);
        }

        public void JumpToWaterfall()
        {
            ExecuteBranch(waterfallStartTime);
        }

        public void JumpToMountain()
        {
            ExecuteBranch(mountainStartTime);
        }

        private void ExecuteBranch(double targetTime)
        {
            if (director != null)
            {
                director.time = targetTime;
                director.Play();
            }

            if (branchingUI != null)
                FadeUI(0f);
        }

        private void FadeUI(float targetAlpha)
        {
            if (_fadeCoroutine != null)
                StopCoroutine(_fadeCoroutine);

            _fadeCoroutine = StartCoroutine(FadeCanvasGroup(targetAlpha));
        }

        private IEnumerator FadeCanvasGroup(float targetAlpha)
        {
            branchingUI.interactable = false;

            float startAlpha = branchingUI.alpha;
            float elapsed = 0f;

            while (elapsed < fadeDuration)
            {
                elapsed += Time.deltaTime;
                branchingUI.alpha = Mathf.Lerp(startAlpha, targetAlpha, elapsed / fadeDuration);
                yield return null;
            }

            branchingUI.alpha = targetAlpha;

            bool isVisible = targetAlpha > 0f;
            branchingUI.blocksRaycasts = isVisible;
            branchingUI.interactable = isVisible;
        }
    }
}
