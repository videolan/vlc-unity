<h3 align="center">
    <img src="/icons/Social_Media_Image_1200x630.png"/>
</h3>

# VLC for Unity

This repository contains the native Unity plugin that bridges [LibVLCSharp](https://code.videolan.org/videolan/LibVLCSharp) with LibVLC for performance oriented video rendering in Unity3D applications and games. Available on the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979).

## Features

Given that this plugin is using [LibVLCSharp](https://code.videolan.org/videolan/LibVLCSharp) (which uses LibVLC), it exposes more or less the [same feature set](https://code.videolan.org/videolan/LibVLCSharp#features) and same codecs support than VLC, such as:

- Plays [all formats](https://wiki.videolan.org/VLC_Features_Formats/)
- Network browsing for distant filesystems (SMB, FTP, SFTP, NFS...).
- HDMI pass-through for Audio HD codecs, like E-AC3, TrueHD or DTS-HD.
- Stream to distant renderers, like Chromecast.
- 360 video and 3D audio playback with viewpoint change.
- Support for Ambisonics audio and more than 8 audio channels.
- Subtitles live size modification.
- Hardware and software decoding on all platforms.
- DVD playback and menu navigation.
- Equalizer support

And more.

## Installing

The recommended way to install the VLC for Unity plugin is through the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979). This also ensures you get commercial support with your build.

## Building

### Windows

- Download and install https://github.com/mstorsjo/llvm-mingw on latest Debian (WSL or otherwise). Add it to path.
- Download VLC nightly build and adjust if need be `vlc-4.0.0-dev/sdk/lib` path to `LDFLAGS` in `Common.mk`
- Build with
```
./build.sh
```
This will produce a `RenderingPlugin.dll` which you can use with LibVLCSharp in your Unity project