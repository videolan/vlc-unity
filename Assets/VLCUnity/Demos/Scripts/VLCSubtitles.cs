using UnityEngine;
using System;
using System.Threading;
using System.Threading.Tasks;
using System.Linq;
using LibVLCSharp;

// this class serves as an example on how to configure playback in Unity with VLC for Unity using LibVLCSharp.
// In this particular sample, we discover how to get and set subtitles information.
// For libvlcsharp usage documentation, please visit https://code.videolan.org/videolan/LibVLCSharp/-/blob/master/docs/home.md
/// On Android, make sure you require Internet access in your manifest to be able to access internet-hosted videos in these demo scenes.

public class VLCSubtitles : MonoBehaviour
{
    [SerializeField] private VLCMediaPlayer mediaPlayer;

    void Start()
    {
        if (mediaPlayer == null || mediaPlayer.MediaPlayer == null)
            return;

        mediaPlayer.OnPlayerStateChanged.AddListener(OnPlayerStateChanged);
    }

    private void OnPlayerStateChanged(VLCState state)
    {
        if (state != VLCState.Playing)
            return;

        var trackList = mediaPlayer.MediaPlayer.Tracks(TrackType.Text);

        foreach (var track in trackList)
        {
            Debug.Log($"Language {track.Language}, id {track.Id}");
        }

        mediaPlayer.MediaPlayer.Select(trackList.Single(t => t.Language.Equals("jpn")));

        // we can also use the track id to select it (11 is the ID of the japanese subtitle track)
        // _mediaPlayer.Select(TrackType.Text, "spu/11");
        
        // if you would like to add an external subtitle file (.srt, .ass, etc.), use the _mediaPlayer.AddSlave method instead
        // https://code.videolan.org/videolan/LibVLCSharp/-/blob/3.x/docs/how_do_I_do_X.md#how-do-i-set-subtitles

        trackList.Dispose();

        mediaPlayer.OnPlayerStateChanged.RemoveListener(OnPlayerStateChanged);
    }

    void OnDestroy()
    {
        mediaPlayer.OnPlayerStateChanged.RemoveListener(OnPlayerStateChanged);
    }
}
