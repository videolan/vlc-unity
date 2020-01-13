<h3 align="center">
    <a href="https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979"><img src="/icons/Social_Media_Image_1200x630.png"/></a>
</h3>

# VLC for Unity

This repository contains the native Unity plugin that bridges [LibVLCSharp](https://code.videolan.org/videolan/LibVLCSharp) with LibVLC for performance oriented video rendering in Unity3D applications and games. Available on the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979).

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

## Installing

The recommended way to install the VLC for Unity plugin is through the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979). This also ensures you get commercial support with your build.

| Platform          |  Unity Store Asset                              |
| ----------------- | ----------------------------------------------- |
| Windows           | [![VLCUnityWindowsBadge]][VLCUnityWindows]      |

[RVLCUnityWindows]: https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979
[VLCUnityWindows]: https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979
[VLCUnityWindowsBadge]: https://img.shields.io/badge/Made%20with-Unity-57b9d3.svg?style=flat&logo=data%3Aimage%2Fpng%3Bbase64%2CiVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAMAAAAolt3jAAABklBMVEUIJCYRLjARLzEWICcbIyYcLDQdJS4dKjMdLTQeKTMeKTUeKjMeKzMeKzQeNDceNTkeNzkeODkfIy8fJi8fJjAfMDQgJzEgKDIgKTIgMTUgMjkhJjAhKDMhKTIhKTQhKzYhLDYhLDchLjUhLjYiKTAiLDciLTgjKjIjLTcjLjkkLTgnKDYnKTYnLjb%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F9oVHO%2FAAAAhXRSTlMAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQUGCAkMDhATFBcZGh0hIyYtNT1IS05RVFZXW1xeYWNnbG9wcXN2eHt9goaKkpWXo6usrbCztLW2ubq7vL2%2Bv8HDxsjKzNfY5OXn6%2Bzt8fP09vj5%2FP3%2BxDGH3QAAAMlJREFUeAFjUFTiZ5AWEFQ1dgwvDuIEc8WkHDJrW1tb07nBXHOb%2FPIYz7LWSgsgl8%2B9NclWjz24LrTVmUFR2b0110SE1aYhyqg%2BmkHRozXNkE2LI67KXDy7iMG7uTUnITU5s9WXhSfQi8GvtbUgMz%2BvsNVLSMbfjUHUpzVRX0VXPb7ClCujiEGSyac1xUhY1q4pwqAulkGSkdmnNd5KTiKsJqDVBcTVtLbPL410LW%2BptgRz5dUcixpbW1qzuMFcBW0dDTOnqJIQXgB6SzT11MCPiQAAAABJRU5ErkJggg%3D%3D

## Getting started

There is a quick `documentation.txt` file [included in the package](./Assets/VLC-Unity-Windows/documentation.txt). It gives some context and high level overview of how things work and how to get started.

Since this Unity plugin largely shares the same API than LibVLCSharp, most LibVLCSharp [samples](https://code.videolan.org/mfkl/libvlcsharp-samples) and [documentation](https://code.videolan.org/videolan/LibVLCSharp/tree/3.x/docs) apply, do check those out!

## Support

Support is provided for paying customers only (i.e. users that installed the plugin from the [Unity Store](https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979)).

## Building

### Windows

- Download and install https://github.com/mstorsjo/llvm-mingw on latest Debian (WSL or otherwise). Add it to path.
- Download VLC nightly build and adjust if need be `vlc-4.0.0-dev/sdk/lib` path to `LDFLAGS` in `Common.mk`
- Build with
```
./build.sh
```
This will produce a `VLCUnityPlugin.dll` which you can use with LibVLCSharp in your Unity project

<br/>
<h3 align="center">
    <a href="https://assetstore.unity.com/packages/tools/video/vlc-for-unity-windows-133979"><img src="icons/Icon_Image_160x160.png"/></a>
</h3>
