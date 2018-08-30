Demo VLC Virtual Cinema
=======================

This Unity project is a demo on how to use LibVLC (the core of VLC
media player) in a Unity project in order to use its functionnality.

With LibVLCSharp, you can use every aspect of the VLC media player in your game.

# Building

## LibVLC for Android + Unity C++ plugin

`git clone https://gitlab.com/videolabs/vlc-unity`

`git checkout libvlcsharp-integration`

`cd VLC-Unity-Wrapper`

`./build-android.sh`

## LibVLCSharp

Build the bindings with VS or `dotnet build` 

`git clone https://code.videolan.org/videolan/LibVLCSharp`

`git checkout feature/unity`

For developing on a Android device, put `vlcunitylib.aar` and `libvlcsharp.dll` in `YourProject\Assets\Plugins\Android\libs\armeabi-v7a`

You need a recent enough version of Unity so that your project understands `.NET Standard 2.0` libraries (`2018.2.5f1` works, but earlier versions might as well).

Unlike on other platforms where you just call `new MediaPlayer`, this is how you create a `MediaPlayer` with LibVLCSharp on Unity:
```
    LibVLC = new LibVLC(new[] { "--verbose=4" });
    MediaPlayer = MediaPlayer.Create(LibVLC);
```

In `UseRenderingPlugin.CallPluginAtEndOfFrames()`, the API you want to retrieve the frame as a texture is this call:

`IntPtr texptr = MediaPlayer.GetFrame(out updated);`