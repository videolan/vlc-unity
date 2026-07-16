using UnityEngine;
using UnityEngine.Playables;
using UnityEngine.Timeline;

namespace LibVLCSharp
{
    public class VLCTimelineClip : PlayableAsset, ITimelineClipAsset
    {
        [Tooltip("Media URL or path. Leave empty to use the bound VLCMediaPlayer's path.")]
        public string mediaPath;

        [Min(0.05f)]
        [Tooltip("Max drift (seconds) before re-syncing the video.")]
        public double syncTolerance = 1.0;

        public ClipCaps clipCaps => ClipCaps.ClipIn | ClipCaps.SpeedMultiplier | ClipCaps.Blending;

        public override Playable CreatePlayable(PlayableGraph graph, GameObject owner)
        {
            var playable = ScriptPlayable<VLCTimelineBehaviour>.Create(graph);
            var behaviour = playable.GetBehaviour();

            behaviour.mediaPath = mediaPath;
            behaviour.syncTolerance = syncTolerance;

            return playable;
        }
    }

    public class VLCTimelineBehaviour : PlayableBehaviour
    {
        public string mediaPath;
        public double syncTolerance;
    }
}
