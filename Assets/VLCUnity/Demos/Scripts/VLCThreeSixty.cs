using UnityEngine;
using System;
using System.Threading.Tasks;
using LibVLCSharp;

public class VLCThreeSixty : MonoBehaviour
{
    [SerializeField] private VLCMediaPlayer mediaPlayerScreen;
    [SerializeField] private VLCMediaPlayer mediaPlayerSphere;
    [SerializeField] private Transform sphereTransform;

    float Yaw, Pitch, Roll;

    void Start()
    {
        // disable default projection for the sphere, no libvlc viewpoint navigation
        if (mediaPlayerSphere != null && mediaPlayerSphere.MediaPlayer != null)
            mediaPlayerSphere.MediaPlayer.SetProjectionMode(VideoProjection.Rectangular);

        if (mediaPlayerScreen != null)
            mediaPlayerScreen.OnPlayerStateChanged.AddListener(OnPlayerStateChanged);
    }

    void OnDestroy()
    {
        if (mediaPlayerScreen != null)
            mediaPlayerScreen.OnPlayerStateChanged.RemoveListener(OnPlayerStateChanged);
    }

    void Update()
    {
        if (mediaPlayerScreen == null || mediaPlayerScreen.MediaPlayer == null)
            return;

        Do360Navigation();
    }

    void Do360Navigation()
    {
        var range = Math.Max(Screen.width, Screen.height);
        Yaw = mediaPlayerScreen.MediaPlayer.Viewpoint.Yaw;
        Pitch = mediaPlayerScreen.MediaPlayer.Viewpoint.Pitch;
        Roll = mediaPlayerScreen.MediaPlayer.Viewpoint.Roll;

        if (VLCInput.MoveRight()) RotateView(20f, 80 * 40 / range, 0);
        else if (VLCInput.MoveLeft()) RotateView(-20f, -80 * 40 / range, 0);
        else if (VLCInput.MoveBack()) RotateView(0, 0, 1);
        else if (VLCInput.MoveForward()) RotateView(0, 0, -1);
    }

    private void OnPlayerStateChanged(VLCState state)
    {
        if (state == VLCState.Playing)
            Check360Projection();
    }

    void Check360Projection()
    {
        var media = mediaPlayerScreen.MediaPlayer?.Media;
        if (media == null)
            return;

        var videoTracks = media.TrackList(TrackType.Video);

        if (videoTracks != null && videoTracks.Count > 0)
        {
            bool is360 = videoTracks[0].Data.Video.Projection == VideoProjection.Equirectangular;
            Debug.Log(is360 ? "The video is a 360 video" : "The video was not identified as a 360 video by VLC");
        }

        videoTracks?.Dispose();
    }

    void RotateView(float sphereRotation, float yawDelta, float pitchDelta)
    {
        // no viewpoint changes for the sphere as the whole video is shown on the texture
        // we can rotate the texture instead
        if (sphereTransform != null)
        {
            sphereTransform.Rotate(Vector3.up * sphereRotation * Time.deltaTime);
            sphereTransform.Rotate(Vector3.right * pitchDelta * 10 * Time.deltaTime);
        }

        Pitch = Mathf.Clamp(Pitch + pitchDelta, -90f, 90f);
        mediaPlayerScreen.MediaPlayer.UpdateViewpoint(Yaw + yawDelta, Pitch, Roll, 80);
    }
}
