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
