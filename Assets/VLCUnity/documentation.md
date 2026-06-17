Welcome to VLC for Unity!

## Docs reference

See the [LibVLCSharp documentation](https://code.videolan.org/videolan/LibVLCSharp/-/blob/master/docs/home.md).

It includes [best practices](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/best_practices.md), [Q&A guide](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/how_do_I_do_X.md), [libvlc specific information](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/libvlc_documentation.md) and [tutorials](https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/tutorials.md).

## Components included

For reference, you need a bunch of components to get this working. On Windows, for example:
- libvlc.dll, libvlccore.dll (and its plugins in /plugins folder): These are nightly build DLLs of the VLC player libraries https://code.videolan.org/videolan/vlc
- Custom build of libvlcsharp, the official VideoLAN C# binding to libvlc https://code.videolan.org/videolan/LibVLCSharp
- VLCUnityPlugin.dll, the VLC-Unity native plugin https://code.videolan.org/videolan/vlc-unity

On Linux, the equivalent files are libvlc.so, libvlccore.so, and libVLCUnityPlugin.so.

This is all included in this package and it all works automatically for you.

LibVLCSharp docs (not Unity specific) https://code.videolan.org/videolan/LibVLCSharp/blob/master/docs/getting_started.md

## Windows

!! You need to set your Unity target platform to "PC, Mac & Linux Standalone" to target Windows classic. Go for the x86_64 architecture.

Both Direct3D 11 and Direct3D 12 graphics APIs are supported.

## Android

For the Unity Android target, we support:
- armeabi-v7a,
- arm64-v8a,
- x86,
- x86_64.

/!\ OpenGL ES is supported on all listed Android architectures. Vulkan is supported only on arm64-v8a builds and requires Android 26 (Android 8.0 Oreo) or higher.

/!\ If the plugin complains about missing binaries and there is an error about this, make sure you have each architecture properly setup in the inspector for each binaries in each VLCUnity/Plugins/Android/libs folders. Select a libvlc.so in any given folder, check the Inspector window of the Unity Editor and make sure the selected "Platform" and "CPU" are correct.

/!\ If the scene starts but no video plays, make sure you set internet access to "required" in Unity player settings. The demo scenes play HTTP videos so your app needs the Android Internet permission.

Reminder: If you want to target arm64-v8a CPU architecture, you must select it in the player settings. To be able to select it, you need to switch the scripting backend to IL2CPP (as opposed to Mono). This is a Unity requirement unrelated to libvlc.

VLC for Unity requires Android 21 (Android 5.0 Lollipop) minimum for OpenGL ES.
For Vulkan support, Android 26 (Android 8.0 Oreo) or higher is required with arm64-v8a architecture.

## UWP

For the Unity UWP target, we support:
- x86_64,
- ARM64.

Both Direct3D 11 and Direct3D 12 graphics APIs are supported.

If you need 32 bit versions, feel free to email us with information regarding your use case at unity@videolabs.io

> In the publisher manifest, make sure the 'InternetClient' capability is enabled so that VLC can access remote streams.

For Hololens support and HTTPS, be aware that the Hololens device has very few SSL certificate by default. This means some HTTPS streams may not work since gnutls cannot find the required certificate. You have 2 options:
1. Install the certificat globally on the device. This is a viable option only if you own the distribution (e.g. can install the required certificate on all client's Hololens devices). This guide should prove helpful: https://learn.microsoft.com/en-us/hololens/certificate-manager
2. Ship your game with the required certificate and tell LibVLC to load using `--gnutls-dir-trust=PATH_TO_CERT_FOLDER` where `PATH_TO_CERT_FOLDER` is a folder inside your appx that contains the cert.

You can export the global certificate from your Windows machine using the certlm.exe app. Look for `GlobalSign Root CA` and export the .per.

## iOS

For the initial release, only ARM64 device builds are provided. Simulator support is not yet included.

It is possible to test things via the Editor on macOS beforehand though, on both Apple Silicon and Apple Intel macOS. And for Apple Silicon users, iOS apps can be ran on the mac.

> For Apple validation errors regarding OS Minimal version of the plugin when pushing to the AppStore, see this issue for a solution: https://code.videolan.org/videolan/vlc-unity/-/issues/227

### iOS plugin coexistence

The VLC iOS plugin registers its rendering callbacks from an Objective-C `+load` method in `Assets/VLCUnity/Plugins/iOS/LoadPlugin.mm`. It intentionally does **not** use the `IMPL_APP_CONTROLLER_SUBCLASS` macro, so it never competes for the global `AppControllerClassName` symbol and coexists with other plugins that do subclass `UnityAppController`. See Unity's [Initialization paths for native iOS plug-ins](https://docs.unity3d.com/6000.5/Documentation/Manual/ios-native-plugin-initialization.html) for background on why only one `IMPL_APP_CONTROLLER_SUBCLASS` can win.

If you still see an `EXC_BAD_ACCESS` crash inside `libvlc_unity_media_player_new` at startup, it means our `+load` never ran and the rendering plugin is unregistered. Most often this is caused by `LoadPlugin.mm` being removed from the Xcode project's **Compile Sources** for the `UnityFramework` target. Re-add it (or re-import the VLC Unity package) and rebuild.

As a last resort, if your project also embeds a custom `UnityAppController` subclass that you cannot modify, add the registration manually from inside that subclass:

```objc
#include "Unity/IUnityGraphics.h"

typedef void (*UnityPluginLoadFunc)(IUnityInterfaces* unityInterfaces);
typedef void (*UnityPluginUnloadFunc)();
extern "C" void UnityRegisterRenderingPluginV5(UnityPluginLoadFunc loadPlugin,
                                               UnityPluginUnloadFunc unloadPlugin);
extern "C" void VLCUnity_UnityPluginLoad(IUnityInterfaces* unityInterfaces);
extern "C" void VLCUnity_UnityPluginUnload();

- (void)shouldAttachRenderDelegate
{
    UnityRegisterRenderingPluginV5(VLCUnity_UnityPluginLoad, VLCUnity_UnityPluginUnload);
}
```

## macOS

Both Apple Silicon (ARM64) and Intel Macs builds are supported, in Editor and through XCode.

The following build scenario is currently unsupported for the beta release:
- Universal builds (binaries with both Intel64 and Apple Silicon binaries) currently are not supported nor tested.

### macOS Plugin Authorization (Editor only)

VLC Unity plugins are currently unsigned. macOS Gatekeeper blocks unsigned code from running in the Unity Editor, which may prevent the plugins from loading during development.

To fix this, use the built-in authorization tool:

1. Go to **Tools > VLC Unity > macOS Plugin Setup**
2. If plugins are blocked, click **Authorize Plugins**
3. Enter your macOS password when prompted

The tool removes the `com.apple.quarantine` extended attribute that macOS applies to downloaded files. A warning will appear in the Console if plugins are blocked, and also before building.

**Note:** Standalone builds are not affected by this and work normally. This authorization is only needed for Editor testing.

**Alternative:** If you have an Apple Developer certificate, you can sign the binaries yourself to permanently resolve this.

## Linux

For the Unity Linux target, we support:
- x86_64.

VLC for Unity requires Ubuntu 22.04 LTS or equivalent (glibc 2.35+).

**Graphics API**: OpenGL only (Vulkan support planned for a future release). Uses GLX on X11 and EGL on XWayland. Select OpenGL in Unity Player Settings.

/!\ The plugin bundles LibVLC 4. System-installed VLC packages (typically VLC 3 on most Linux distributions) are not used.

/!\ Native Wayland support (without XWayland) is planned for a future release.

/!\ If the scene starts but no video plays, ensure your system has working OpenGL drivers. Check with `glxinfo | grep "OpenGL version"`.

### Linux Troubleshooting

DMA-BUF texture sharing requires the **DRI3** X11 extension and a hardware GPU driver. If DRI3 is missing or your system falls back to software rendering (llvmpipe), video will not display.

**Check DRI3 availability:**
```
xdpyinfo -queryExtensions | grep DRI
```
You should see `DRI3` in the output.

**Check GPU renderer:**
```
glxinfo | grep "OpenGL renderer"
```
You should see your hardware GPU (e.g. `Mesa Intel(R) Iris(R) Xe Graphics`), not `llvmpipe`.

**Common causes of DRI3/GPU issues:**
- **Outdated XWayland**: older XWayland versions may not expose DRI3, causing glamor to fall back to software rendering. Update your XWayland package.
- **simpledrm conflict**: on EFI systems, the `simpledrm` framebuffer driver may claim `card0` during early boot, pushing the real GPU to `card1`. XWayland/glamor may fail to use a non-`card0` device. Workaround: use a native Xorg session instead of Wayland+XWayland, or use a kernel with `CONFIG_DRM_SIMPLEDRM=n`.
- **Missing GPU driver**: install Mesa OpenGL and VA-API drivers for your hardware (`mesa-utils`, `mesa-va-drivers`, etc.).

## General

The scenes are located in `Assets/VLCUnity/Demos/Scenes` and provide a way to get started quickly. 

Select any scene (*.unity) and press play in the Unity Editor (or make a standalone build), and the video will start playing.

## VLCMediaPlayer Component

The plugin provides a centralized `VLCMediaPlayer` component for easy integration:

**Quick Setup:**
1. Add `VLCMediaPlayer` component to any GameObject
2. Configure media path, audio, and texture options in the Inspector
3. Use `VLCDisplayMesh` or `VLCDisplayUGUI` to automatically apply video to renderers or UI

**Key Features:**
- Drag-and-drop setup with custom Inspector
- Event-driven architecture (`OnPlayerStateChanged`, `OnTextureResized`)
- Async media loading for network streams (`OpenAsync`)
- Built-in controls for volume, seeking, track selection, and subtitles
- Configurable texture orientation handling for Unity display targets
- Multiple players per scene with shared LibVLC instance

**Helper Components:**
- `VLCDisplayMesh` - Applies video texture to any 3D mesh/renderer
- `VLCDisplayUGUI` - Applies video texture to UI RawImage elements

### Demo Scene Orientation Notes

Video orientation depends on both the native texture backend and the Unity surface that displays the texture. The demo scenes intentionally do not all use the same correction values: mesh UVs, UI `RawImage` UVs, and platform graphics backends can have different coordinate origins.

Do not normalize `flipTextureX`, `flipTextureY`, or `RawImage.uvRect` values across all demos without testing the affected display path.

| Scene or display path | Windows | Android | Linux | Notes |
| --- | --- | --- | --- | --- |
| `VLCMinimalPlayback`, `VLCYouTubePlayback`, `VLCTransparentVideoPlayback`, `VLCSubtitles` using `VLCDisplayMesh` | `flipTextureX=false`, `flipTextureY=false` | Same expected setup | Same expected setup | These use a regular mesh screen and the shared `VLCMediaPlayer.OutputTexture`. |
| `VLC 3D Example` using `VLCDisplayMesh` | `flipTextureX=true`, `flipTextureY=true` | Same scene-level correction expected | Same scene-level correction expected | The cinema-room screen mesh has its own UV/coordinate orientation, so this scene keeps explicit player flips. |
| `VLC Canvas Example` / `VLCDisplayUGUI` using `RawImage` | `VLCDisplayUGUI` applies `RawImage.uvRect = Rect(1, 1, -1, -1)` | `VLCDisplayUGUI` applies `RawImage.uvRect = Rect(1, 1, -1, -1)` | `VLCDisplayUGUI` applies `RawImage.uvRect = Rect(1, 1, -1, -1)` | UI rendering has a different texture coordinate path than mesh rendering. The Canvas path corrects both UI axes locally instead of changing the shared `VLCMediaPlayer` flips. |
| Custom UI with `RawImage` | Start from `VLCDisplayUGUI` | Start from `VLCDisplayUGUI` | Start from `VLCDisplayUGUI` | If a custom UI is mirrored or upside down, prefer correcting the UI `uvRect` instead of changing the shared `VLCMediaPlayer` flips. |

When diagnosing orientation, use a test frame with identifiable corners. For example, if content that should appear at bottom right appears at bottom left, the X axis is mirrored. If it appears at top right, the Y axis is mirrored. If it appears at top left, both axes are mirrored.

**Example Usage:**
```csharp
[SerializeField] private VLCMediaPlayer mediaPlayer;

void Start()
{
    mediaPlayer.OnPlayerStateChanged.AddListener((state) => {
        Debug.Log($"Player state: {state}");
    });
    
    mediaPlayer.OnTextureResized += (texture) => {
        Debug.Log($"Video size: {texture.width}x{texture.height}");
    };
}

public void PlayVideo(string url)
{
    mediaPlayer.OpenAsync(url);
}
```

## Advanced: Manual Integration

The following information provides context on how the VLCMediaPlayer component works internally.
For most use cases, the VLCMediaPlayer component is all you need. See the section above.

The VLCMediaPlayer component handles all of this automatically:

1. Loading native libraries (libvlc):
```
Core.Initialize(UnityEngine.Application.dataPath);
```

2. Creating LibVLCSharp objects and starting playback:
```
LibVLC = new LibVLC();
MediaPlayer = new MediaPlayer(LibVLC);
MediaPlayer.Play(new Media(new Uri("http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4")));
```

3. Texture creation and frame updating after video dimensions are available:
```
Texture2D vlcTexture = null;
RenderTexture outputTexture = null;

// In Update():
if (vlcTexture == null)
{
    vlcTexture = TextureHelper.CreateNativeTexture(MediaPlayer, linear: true);
    if (vlcTexture != null)
    {
        outputTexture = new RenderTexture(vlcTexture.width, vlcTexture.height, 0, RenderTextureFormat.ARGB32);
        outputTexture.Create();
    }
}

if (vlcTexture != null && TextureHelper.UpdateTexture(vlcTexture, MediaPlayer))
{
    Graphics.Blit(vlcTexture, outputTexture);
}
```

To get up and running easily and quickly, load the Assets/VLCUnity/Demos/Scenes/VLCMinimalPlayback.unity scene and press "Play".
The scene uses the VLCMediaPlayer component with all configuration visible in the Inspector.

## Scenes

- A minimal playback example with buttons,
- 360 playback with keyboard navigation built-in,
- A video with subtitles showcasing support,
- The VLC Canvas Example provides a UI-based player with more controls,
- 3D scene you can move around in with a movie screen and chairs in a cinema room.

For more API usage information, explore our [online docs](https://code.videolan.org/videolan/LibVLCSharp/-/blob/master/docs/home.md).

For VLC Unity specific questions and support, open an issue on our GitLab and browse our opensource plugin code at https://code.videolan.org/videolan/vlc-unity

We also provide support through StackOverflow, you may browse the [libvlcsharp](https://stackoverflow.com/questions/tagged/libvlcsharp) and [vlc-unity](https://stackoverflow.com/questions/tagged/vlc-unity) tags.

## Misc

/!\ There seems to be issues when using VLC Unity and MRTK in the same Unity project. Both LibVLCSharp and MRTK rely on System.Numerics.Vectors and this triggers an issue with IL2CPP. 
Removing one of the System.Numerics.Vectors.dll in your project seems to be a valid workaround (but be aware of versioning mismatch).

## Asset import management

All releases include binaries for all supported platforms.

VLC Unity is distributed as either a trial package or a pro package:
- Trial packages include all supported platforms, with watermarked video output and a 60 second playback limit per session.
- Pro packages include all supported platforms without trial limitations.

VLC Unity is provided as standalone .unitypackage files that you can import using the Unity Editor. You no longer need to merge separate per-platform paid packages or selectively import platform-specific binaries.
