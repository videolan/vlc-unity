<h3 align="center">
    <a href="https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979"><img src="/icons/Social_Media_Image_1200x630.png"/></a>
</h3>

# VLC for Unity

[![Join the chat at https://discord.gg/3h3K3JF](https://img.shields.io/discord/716939396464508958?label=discord)](https://discord.gg/3h3K3JF)

This repository contains the native Unity plugin that bridges [LibVLCSharp](https://code.videolan.org/videolan/LibVLCSharp) with LibVLC for performance oriented video rendering in Unity3D applications and games. Available on the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979).

- [VLC for Unity](#VLC-for-Unity)
  - [LibVLC Features](#LibVLC-Features)
  - [Supported platforms](#Supported-platforms)
  - [Installation](#Installation)
  - [Getting started](#Getting-started)
  - [Documentation](#Documentation)
  - [Support](#Support)
    - [StackOverflow](#stackoverflow)
  - [Sample scenes](#Sample-scenes)
  - [Roadmap](#Roadmap)
    - [Platforms support](#Platforms-support)
    - [Graphics APIs](#Graphics-APIs)
    - [Other improvements](#Other-improvements)
  - [Release Notes](#Release-Notes)
  - [Building](#Building)
  - [Code of Conduct](#Code-of-Conduct)
  - [Commercial Services](#Commercial-services)
  - [License](#License)

## LibVLC Features

Given that this plugin is using [LibVLCSharp](https://code.videolan.org/videolan/LibVLCSharp) (which uses LibVLC), it exposes more or less the [same feature set](https://code.videolan.org/videolan/LibVLCSharp#features) and same codecs support than VLC, such as:

- Play every media file formats, every codec and every streaming protocols
- Run on every platform, from desktop (Windows, Linux, Mac) to mobile (Android, iOS) and TVs
- Hardware and efficient decoding on every platform, up to 8K
- Network browsing for distant filesystems (SMB, FTP, SFTP, NFS...) and servers (UPnP, DLNA)
- Playback of Audio CD, DVD and Bluray with menu navigation
- Support for HDR, including tonemapping for SDR streams
- Audio passthrough with SPDIF and HDMI, including for Audio HD codecs, like DD+, TrueHD or DTS-HD
- Support for video and audio filters
- Support for 360 video and 3D audio playback, including Ambisonics
- Able to cast and stream to distant renderers, like Chromecast and UPnP renderers.

And more.

## Supported platforms

- Windows Classic x64

## Installation

The recommended way to install the VLC for Unity plugin is through the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979). This also ensures you get commercial support with your build.

| Platform          |  Unity Store Asset                              |
| ----------------- | ----------------------------------------------- |
| Windows           | [![VLCUnityWindowsBadge]][VLCUnityWindows]      |

[RVLCUnityWindows]: https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979
[VLCUnityWindows]: https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979
[VLCUnityWindowsBadge]: https://img.shields.io/badge/Made%20with-Unity-57b9d3.svg?style=flat&logo=unity

#### [Download the Free Trial Version](https://videolabs.io/#unity).

## Getting started

There is a quick `documentation.txt` file [included in the package](./Assets/VLCUnity/documentation.txt). It gives some context and high level overview of how things work and how to get started.

Since this Unity plugin largely shares the same API than LibVLCSharp, most LibVLCSharp [samples](https://code.videolan.org/mfkl/libvlcsharp-samples) and [documentation](https://code.videolan.org/videolan/LibVLCSharp/tree/3.x/docs) apply, do check those out!

## Documentation

See the [LibVLCSharp documentation](https://code.videolan.org/videolan/LibVLCSharp/tree/master/docs).

It includes [best practices](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/best_practices.md), [Q&A guide](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/how_do_I_do_X.md), [libvlc specific information](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/libvlc_documentation.md) and [tutorials](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/tutorials.md).

## Support

Support is done on a volunteer base, professional support is only available for [paying customers](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979).

### StackOverflow

We regularly monitor the [![stackoverflow-libvlcsharp](https://img.shields.io/stackexchange/stackoverflow/t/libvlcsharp.svg?label=libvlcsharp&style=flat)](https://stackoverflow.com/questions/tagged/libvlcsharp) and [![stackoverflow-vlc-unity](https://img.shields.io/stackexchange/stackoverflow/t/vlc-unity.svg?label=vlc-unity&style=flat)](https://stackoverflow.com/questions/tagged/vlc-unity) tags on StackOverflow.

## Sample scenes

Several Unity [sample scenes](./Assets/VLCUnity/Demos/Scenes) are available to help you get started quickly!

- [A minimal playback example with buttons](./Assets/VLCUnity/Demos/Scripts/MinimalPlayback.cs)
- [360 playback with keyboard navigation built-in](./Assets/VLCUnity/Demos/Scripts/ThreeSixty.cs)

More LibVLCSharp samples can be found [here](https://code.videolan.org/mfkl/libvlcsharp-samples).

## Roadmap

### Platforms support

- Android (prototype working),
- macOS, iOS, tvOS,
- UWP,
- Linux.

### Graphics APIs

- OpenGL ES,
- OpenGL,
- Metal.

### Other improvements

- Provide various easy to use, ready-to-run samples and improved documentation.
- Provide in-Editor MediaPlayer customization, so you can customize LibVLC and playback parameters directly from the Unity Editor.

## Release Notes

Check out the [NEWS](NEWS) file for details about each release.

## Building

### Windows

- Download and install https://github.com/mstorsjo/llvm-mingw on latest Debian (WSL or otherwise). Add it to path.
- run `./get-libvlc-windows.sh` to download a win64 llvm libvlc 4 build.
- Build with
```
./build.sh
```
This will produce a `VLCUnityPlugin.dll` which you can use with LibVLCSharp in your Unity project
- Extract the `vlc-cache-gen.exe` binary from the nightly build archive. Drag and drop the `plugins` folder onto it. Or just run the Unity scene with `_libVLC = new LibVLC("--reset-plugins-cache");`. This will produce a `plugins.dat` file in the `plugins` folder.

## Debugging

Editor crash location: `C:\Users\YourUserName\AppData\Local\Temp\Unity\Editor\Crashes`

## Code of Conduct

Please read and follow the [VideoLAN CoC](https://wiki.videolan.org/Code_of_Conduct/).

## Commercial services

If you would like VLC developers to provide you with:
- custom development on LibVLC, VLC for Unity and/or LibVLCSharp, 
- training and workshops,
- support services,
- consulting services,
- other multimedia services.

Feel free to [contact us](https://videolabs.io/#contact).

## License

Licensed under the LGPL 2.1 and prorietary license via [Videolabs](https://videolabs.io/).

<br/>
<div align="center">
    <a href="https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979"><img src="icons/Icon_Image_160x160.png"/></a>
</div>
