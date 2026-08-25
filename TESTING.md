# Running the Unity tests

## Prerequisites

- Install Unity `6000.3.10f1` through Unity Hub. This is the version recorded in `ProjectSettings/ProjectVersion.txt`.
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
