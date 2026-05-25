using UnityEngine;
using LibVLCSharp;

public class VLCUIControls : MonoBehaviour
{
    [SerializeField] private VLCMediaPlayer mediaPlayer;
    [SerializeField] private long seekTimeDeltaMs = 5000;

    public void SeekForward()
    {
        if (mediaPlayer != null)
            mediaPlayer.Seek(seekTimeDeltaMs);
    }

    public void SeekBackward()
    {
        if (mediaPlayer != null)
            mediaPlayer.Seek(-seekTimeDeltaMs);
    }
}
