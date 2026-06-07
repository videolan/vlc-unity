using System.Collections.Generic;
using UnityEngine;

public enum VLCPlaylistLoopMode
{
    None,
    Single,
    All
}

public enum VLCTransitionType
{
    Cut,            // Hard cut, bypasses shader entirely
    Crossfade,      // Standard alpha lerp
    SmoothWipe,     // UV-based directional wipe
    LumaDissolve,   // Organic thresholding using a noise map
    RadialReveal,   // Expanding circle from UV center
    Glitch          // Chromatic shift and UV displacement
}

[CreateAssetMenu(fileName = "NewPlaylist", menuName = "VLC/Playlist")]
public class VLCPlaylistAsset : ScriptableObject
{
    [Header("Playlist Configuration")]
    public List<VLCPlaylistItem> items = new();
    public VLCPlaylistLoopMode loopMode = VLCPlaylistLoopMode.None;
    public bool shuffle = false;

    [Header("Global Transitions")]
    public VLCTransitionType defaultTransition = VLCTransitionType.Crossfade;
    [Range(0.0f, 5f)] public float defaultTransitionDuration = 2.0f;

    [Tooltip("Assign a grayscale noise map here for the LumaDissolve effect.")]
    public Texture2D transitionNoiseMap;
}
