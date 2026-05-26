using UnityEngine;
using System;
using System.Threading.Tasks;
using System.Collections;
using LibVLCSharp;
using UnityEngine.UI;


public class VLCCastExample : MonoBehaviour
{
    [SerializeField] private VLCMediaPlayer mediaPlayer;
    public Text text;
    private RendererItem rendererItem;
    private RendererDiscoverer rendererDiscoverer;

    void Start()
    {
        if (VLCMediaPlayer.LibVLC != null)
        {
            VLCMediaPlayer.LibVLC.SetDialogHandlers(
                (dialog, title, text, username, store, token) => Task.CompletedTask,
                (dialog, title, text, type, cancelText, actionText, secondActionText, token) =>
                {
                    ScreenLog("QuestionCallback called");
                    var result = dialog.PostAction(1);
                    ScreenLog("QuestionCallback PostAction " + result.ToString());
                    return Task.CompletedTask;
                },
                (dialog, title, text, indeterminate, position, cancelText, token) => Task.CompletedTask,
                (dialog, position, text) => Task.CompletedTask);
        }

        StartCoroutine(WaitAndDiscover());
    }

    void OnDisable()
    {
        rendererDiscoverer?.Dispose();
        rendererDiscoverer = null;
        rendererItem = null;
    }

    public void Open()
    {
        if (mediaPlayer != null && mediaPlayer.MediaPlayer != null && rendererItem != null)
        {
            ScreenLog("remote media player created");
            // set the previously discovered renderer item (chromecast) on the mediaplayer
            if (mediaPlayer.MediaPlayer.SetRenderer(rendererItem))
            {
                mediaPlayer.MediaPlayer.Media = new Media(new Uri(mediaPlayer.mediaPath));
            }
        }
    }

    public void Play()
    {
        mediaPlayer?.Play();
        ScreenLog("Play called.");
    }

    void RendererDiscoverer_ItemAdded(object sender, RendererDiscovererItemAddedEventArgs e)
    {
        ScreenLog("ItemAdded");
        rendererItem = e.RendererItem;
        ScreenLog("rendererItem.name = " + rendererItem.Name);
    }

    IEnumerator WaitAndDiscover()
    {
        yield return new WaitForSeconds(5.0f);
        if (mediaPlayer != null && mediaPlayer.MediaPlayer != null)
        {
            ScreenLog("attempt to create RendererDiscoverer ...");
            rendererDiscoverer = new RendererDiscoverer(VLCMediaPlayer.LibVLC);
            if (rendererDiscoverer != null)
            {
                ScreenLog("success.");
                rendererDiscoverer.ItemAdded += RendererDiscoverer_ItemAdded;
                rendererDiscoverer.Start();
            }
        }

        yield return new WaitForSeconds(5.0f);
        if (rendererItem != null)
        {
            Open();
            yield return new WaitForSeconds(5.0f);
            Play();
        }
    }

    void ScreenLog(string message)
    {
        if (text != null)
        {
            text.text += message + "\n";
        }
        Debug.Log(message);
    }
}
