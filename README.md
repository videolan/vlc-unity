# Building

## Windows

- Download and install https://github.com/mstorsjo/llvm-mingw on latest Debian (WSL or otherwise). Add it to path.
- Download VLC nightly build and adjust if need be `vlc-4.0.0-dev/sdk/lib` path to `LDFLAGS` in `Common.mk`
- Build with
```
./build.sh
```
This will produce a `RenderingPlugin.dll` which you can use with LibVLCSharp in your Unity project