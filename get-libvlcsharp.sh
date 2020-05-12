#! /bin/bash

# this script assumes there is the libvlcsharp repo next to vlc-unity

rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/LibVLCSharp.dll

msbuild ../libvlcsharp/src/LibVLCSharp/LibVLCSharp.csproj /p:Unity=true,Configuration=Release

cp ../LibVLCSharp/src/LibVLCSharp/bin/Release/netstandard2.0/LibVLCSharp.dll Assets/VLC-Unity-Windows/Plugins/x86_64/