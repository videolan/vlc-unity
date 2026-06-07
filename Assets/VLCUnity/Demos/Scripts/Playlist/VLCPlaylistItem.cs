using System;
using UnityEngine;

[Serializable]
public struct VLCPlaylistItem
{
    [Header("Metadata")]
    public string title;
    public string path;
    [TextArea(1, 3)] public string[] options;

    [Header("Transition Override")]
    public bool overridePlaylistTransition;
    public VLCTransitionType transitionType;
    [Range(0.0f, 5f)] public float transitionDuration;
}
