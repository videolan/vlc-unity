# Running the Unity tests

## Prerequisites

- Install the Unity version recorded in `ProjectSettings/ProjectVersion.txt` through Unity Hub.
- Make sure the managed `LibVLCSharp.dll` is available. Binary plugin files are intentionally ignored by Git.
  - A packaged VLC for Unity build already contains it.
  - On Windows, place it at `Assets/VLCUnity/Plugins/Windows/x86_64/LibVLCSharp.dll`.
  - To build it from a sibling LibVLCSharp checkout:

    ```powershell
    dotnet build ..\LibVLCSharp\src\LibVLCSharp\LibVLCSharp.csproj /p:UNITY=true -c Release
    Copy-Item ..\LibVLCSharp\src\LibVLCSharp\bin\Release\netstandard2.0\LibVLCSharp.dll Assets\VLCUnity\Plugins\Windows\x86_64\
    ```

The Unity Test Framework dependency is declared in `Packages/manifest.json` and is restored automatically. Native LibVLC and VLC Unity plugin binaries are required for playback testing, but not for the logging Edit Mode tests. These tests do not exercise the native `SetLogCallback` entry point, so validate a freshly built native plugin before packaging.

## Run in the Unity Editor

1. Open the repository as a Unity project and wait for script compilation to finish.
2. Open **Window > General > Test Runner**.
3. Select **EditMode**.
4. Run `LibVLCSharp.Tests.VLCUnityLoggerTests`, or use **Run All**.

## Run from the command line

Close any Unity Editor instance that has the project open, then run:

```powershell
& "C:\Program Files\Unity\Hub\Editor\6000.3.10f1\Editor\Unity.exe" `
  -batchmode `
  -projectPath (Get-Location) `
  -runTests `
  -testPlatform EditMode `
  -testFilter LibVLCSharp.Tests.VLCUnityLoggerTests `
  -testResults TestResults.xml `
  -logFile TestRun.log
```

Unity exits when the test run finishes. Do not add `-quit`, because it can make Unity exit before the tests start.

## Run the native Linux policy tests

The native tests are headless. They validate Linux backend selection, stable
DRM render-node enumeration, explicit device overrides, and compatibility-probe
fallback without requiring Unity or a GPU:

```bash
meson setup build-linux-tests
meson compile -C build-linux-tests
meson test -C build-linux-tests --print-errorlogs
```

The Linux suite includes `linux-graphics-interop`, `linux-vulkan-identity`, and
`unique-fd`, and — when Vulkan development headers are available —
`vulkan-core`, `vulkan-requirements`, `owned-queue-submission`, and
`unity-submission-strategy`.

The full Meson configuration still requires the normal LibVLC development
dependency. CI should run this test on both x86_64 and arm64 Linux builders when
arm64 packaging is added; the tested policy code has no architecture-specific
assumptions.

## Linux GPU integration matrix

Headless unit tests cannot validate driver-owned GLX/EGL sharing or external
memory imports. Before publishing a Linux package, run `VLCMinimalPlayback`
with OpenGLCore and Vulkan in the Linux Editor, a standalone player, and an
Embedded Linux player. Restart the Editor after importing or updating the
preloaded plugin before running its Vulkan case. Confirm that OpenGL reports a
successful shared-context or compatible DMA-BUF path and Vulkan reports active
initialization interception plus a compatible DMA-BUF device.

Cover these configurations when hardware is available:

| Session | GPU topology | Expected path |
| --- | --- | --- |
| Xorg | Single Intel/AMD/NVIDIA GPU | GLX shared context |
| XWayland | Single GPU | GLX shared context |
| Xorg or XWayland | Integrated + NVIDIA PRIME | GLX shared context on Unity's selected GPU |
| Forced DMA-BUF test | Multiple render nodes | Reject incompatible nodes and select the matching node |
| Native Wayland experimental | Single GPU | EGL plus DMA-BUF |
| Linux Editor after restart | Single Mesa GPU | Vulkan interception and DMA-BUF copy |
| Standalone Linux | Single Mesa GPU | Vulkan interception and DMA-BUF copy |
| Embedded Linux | Supported GBM/Vulkan GPU | Vulkan interception and DMA-BUF copy |

For performance captures, do not pass Unity `-nographics`: that disables the
path being measured. A machine may be physically headless if it exposes a real
GPU and uses Xvfb, a compositor, or a supported surfaceless setup. Pin the GPU
and driver baseline and record main/render-thread p50/p95 time, GPU copy and
total frame time, queue submissions, dropped frames, and FD/native-memory
counts for one, two, and four players at 1080p and 4K. Software
llvmpipe/lavapipe is useful for functional smoke coverage, not representative
timing.

For a forced-device run:

```bash
VLC_UNITY_GLX_FORCE_DMABUF=1 \
VLC_UNITY_DRM_DEVICE=/dev/dri/renderD129 \
./YourGame.x86_64 -force-glcore 2>&1 | tee vlc-unity-linux.log
```

For backend A/B testing:

```bash
VLC_UNITY_LINUX_OPENGL_BACKEND=glx ./YourGame.x86_64 -force-glcore
VLC_UNITY_LINUX_OPENGL_BACKEND=egl ./YourGame.x86_64 -force-glcore
```

## Automated Linux graphics regressions

These tests assert working behaviour; failures are not marked as expected passes
or skipped.

Use the Unity version in `ProjectSettings/ProjectVersion.txt`, with Linux
Standalone and Embedded Linux support installed.
Supply the normal Linux native binaries and Unity-enabled `LibVLCSharp.dll`.
No downloaded video or ffmpeg is needed: each player generates a local 64x64
red/green/blue YUV4MPEG2 fixture. The LibVLC build must include its Y4M demuxer
and raw-video decoder.

Close the Editor for the project being tested, or use an isolated project copy.
A real GPU, accessible DRM render node, and the requested display server are
required. For the complete matrix use a Wayland desktop with XWayland, with both
`DISPLAY` and `WAYLAND_DISPLAY` set. Native Xorg coverage requires a separate
Xorg session. A missing requested display is a failure, not a silent skip.

Run the standalone matrix from the project root:

```bash
UNITY_EDITOR=/path/to/Unity/Editor/Unity
VLC_UNITY_RUN_GRAPHICS_TESTS=1 \
VLC_UNITY_GRAPHICS_RESULTS=/tmp/vlc-graphics-results \
"$UNITY_EDITOR" -batchmode -nographics -projectPath "$PWD" \
  -buildTarget Linux64 -runTests -testPlatform EditMode \
  -testFilter LibVLCSharp.Tests.LinuxGraphicsCliTests \
  -testResults /tmp/vlc-graphics-linux.xml -logFile /tmp/vlc-graphics-linux-cli.log
```

Run the Embedded Linux matrix:

```bash
VLC_UNITY_RUN_GRAPHICS_TESTS=1 \
VLC_UNITY_GRAPHICS_RESULTS=/tmp/vlc-graphics-results \
"$UNITY_EDITOR" -batchmode -nographics -projectPath "$PWD" \
  -buildTarget EmbeddedLinux -runTests -testPlatform EditMode \
  -testFilter LibVLCSharp.Tests.LinuxGraphicsCliTests \
  -testResults /tmp/vlc-graphics-embedded.xml -logFile /tmp/vlc-graphics-embedded-cli.log
```

Do not add `-quit`. The Test Runner exits on completion and produces NUnit XML;
its exit status indicates success/failure. Here `-nographics` applies **only to
the orchestrating Editor**. The tests launch separate GPU-backed players without
`-nographics` or `-batchmode`, and enable background updates so an external VLC
window cannot turn lost Unity focus into a false success.

The suite builds one dedicated player per invocation, containing only a temporary
empty scene and the test bootstrap. It enables both relevant graphics APIs for
that build, restores the project's graphics API settings in `finally`, and removes
its uniquely named generated scene. It does not rebuild/deploy native libraries,
change renderer implementations, or replace production scenes. The probe assembly
is gated by the build-only `VLC_UNITY_GRAPHICS_TEST_PLAYER` define.

### Cases and assertions

| Test name | Window system | Bridge | Initialization |
| --- | --- | --- | --- |
| `Auto_X11_Cold` | X11/XWayland | Automatic, expect GLX | Cold |
| `Auto_Wayland_Cold` | Native Wayland, DISPLAY also set | Automatic, expect EGL | Cold |
| `GLX_X11_cold`, `GLX_X11_warm` | X11/XWayland | Explicit GLX | Both |
| `EGL_X11_cold`, `EGL_X11_warm` | X11/XWayland | Explicit EGL | Both |
| `EGL_Wayland_cold`, `EGL_Wayland_warm` | Native Wayland | Explicit EGL | Both |
| `Vulkan_X11`, `Vulkan_Wayland` | X11 or native Wayland | Vulkan | Cold |
| `NativeLibrariesAreLoadedOnce` | X11/XWayland | GLX | Warm; also asserts unique mappings |

For one case, replace the filter with, for example,
`LibVLCSharp.Tests.LinuxGraphicsCliTests.EGL_Wayland_cold`. On an Xorg-only machine,
select X11 cases rather than treating unavailable Wayland coverage as a pass.

Cold construction happens at `AfterAssembliesLoaded`, before the production
`OnLoad` render event at `BeforeSceneLoad`. Warm construction waits for the
native Unity-context-capture log marker. The checker verifies that the requested
order really occurred; arbitrary sleeps or an accidentally warm run cannot pass
the cold test.

Each case asserts the actual graphics API, selected bridge, display backend,
GLX/EGL context observed **on the render thread after plugin work** (OpenGL cases),
and all three fixture colors in the 64x64 Unity output texture. Pixel readback
runs at `WaitForEndOfFrame`, after rendering and queued texture work.
A static/non-video/black texture,
wrong backend, lost context, missing result, or timeout is not success. Vulkan
does not use the OpenGL context observer; its binding field is zero (not applicable).
GLX on native Wayland is intentionally not a case: GLX requires X11/XWayland.

The runtime probe has a 20-second deadline after managed bootstrap. The parent
has a 45-second wall-clock watchdog including startup and shutdown, and kills only
its own test player if necessary (up to five more seconds each for stopping and
draining output). A pixel-success JSON followed by a shutdown hang
still fails the CLI test, with both pieces of evidence retained.

Every run gets a unique artifacts directory under `VLC_UNITY_GRAPHICS_RESULTS`
(default: ignored `build-linux-graphics-tests/`). Each case saves `player.log`,
`console.log`, `colors.y4m`, `result.json` when managed code completes, and
`assertions.txt`. JSON includes the API, context binding, colors seen, sample count,
and mapped bridge/LibVLC/core paths. The packaging test checks those paths separately
so a duplicated library cannot be mistaken for a video-decoding failure.

### Fast tests for the result checker

These need neither GPU access nor native libraries and do not build a player:

```bash
"$UNITY_EDITOR" -batchmode -nographics -projectPath "$PWD" \
  -runTests -testPlatform EditMode \
  -testFilter LibVLCSharp.Tests.LinuxGraphicsAssertionsTests \
  -testResults /tmp/vlc-graphics-oracle.xml -logFile /tmp/vlc-graphics-oracle.log
```

Ordinary Run All skips the expensive graphics fixture unless
`VLC_UNITY_RUN_GRAPHICS_TESTS=1` is explicitly set. The CLI commands above opt in;
missing prerequisites or broken results in an opted-in run are failures.
