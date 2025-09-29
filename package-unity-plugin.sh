#!/bin/bash
set -e

MODE=$1

if [[ "$MODE" != "trial" && "$MODE" != "pro" ]]; then
  echo "Usage: $0 [trial|pro]"
  exit 1
fi

echo "Packaging Unity plugins in '$MODE' mode..."

# UWP
mkdir -p Plugins/WSA/UWP/ARM64
mkdir -p Plugins/WSA/UWP/x86_64

cp vlc-win64-uwp/VLCUnityPlugin.dll Plugins/WSA/UWP/x86_64
cp vlc-win64-uwp/win64-uwp/vlc-4.0.0-dev/libvlc.dll Plugins/WSA/UWP/x86_64
cp vlc-win64-uwp/win64-uwp/vlc-4.0.0-dev/libvlccore.dll Plugins/WSA/UWP/x86_64
cp -r vlc-win64-uwp/win64-uwp/vlc-4.0.0-dev/plugins Plugins/WSA/UWP/x86_64

cp vlc-winarm64-uwp/VLCUnityPlugin.dll Plugins/WSA/UWP/ARM64
cp vlc-winarm64-uwp/winarm64-uwp/vlc-4.0.0-dev/libvlc.dll Plugins/WSA/UWP/ARM64
cp vlc-winarm64-uwp/winarm64-uwp/vlc-4.0.0-dev/libvlccore.dll Plugins/WSA/UWP/ARM64
cp -r vlc-winarm64-uwp/winarm64-uwp/vlc-4.0.0-dev/plugins Plugins/WSA/UWP/ARM64

# Windows
mkdir -p Plugins/Windows/x86_64

cp vlc-win64/VLCUnityPlugin.dll Plugins/Windows/x86_64
cp vlc-win64/win64/vlc-4.0.0-dev/libvlc.dll Plugins/Windows/x86_64
cp vlc-win64/win64/vlc-4.0.0-dev/libvlccore.dll Plugins/Windows/x86_64
cp vlc-win64/win64/vlc-4.0.0-dev/vlc-cache-gen.exe Plugins/Windows/x86_64
cp -r vlc-win64/win64/vlc-4.0.0-dev/plugins Plugins/Windows/x86_64
cp -r vlc-win64/win64/vlc-4.0.0-dev/lua Plugins/Windows/x86_64
cp lvs-all/netstandard2.0/LibVLCSharp.dll Plugins/Windows/x86_64

# macOS
MAC_ARCHS=("aarch64" "x86_64")
mkdir -p Plugins/MacOS/ARM64
mkdir -p Plugins/MacOS/x86_64

cp build_${MAC_ARCHS[0]}/PluginSource/libVLCUnityPlugin.dylib Plugins/MacOS/ARM64
cp build_${MAC_ARCHS[1]}/PluginSource/libVLCUnityPlugin.dylib Plugins/MacOS/x86_64

cp -r macos-${MAC_ARCHS[0]}/macos-install/lib/vlc Plugins/MacOS/ARM64
cp macos-${MAC_ARCHS[0]}/macos-install/lib/libvlc.dylib Plugins/MacOS/ARM64
cp macos-${MAC_ARCHS[0]}/macos-install/lib/libvlccore.dylib Plugins/MacOS/ARM64

cp -r macos-${MAC_ARCHS[1]}/macos-install/lib/vlc Plugins/MacOS/x86_64
cp macos-${MAC_ARCHS[1]}/macos-install/lib/libvlc.dylib Plugins/MacOS/x86_64
cp macos-${MAC_ARCHS[1]}/macos-install/lib/libvlccore.dylib Plugins/MacOS/x86_64

# iOS
mkdir -p Plugins/iOS

mv Assets/VLCUnity/Plugins/iOS Plugins
cp lvs-ios/netstandard2.0/LibVLCSharp.dll Plugins/iOS

# Android
if [[ "$MODE" == "trial" ]]; then
  ANDROID_ARCHS=("armeabi-v7a" "arm64-v8a")
else
  ANDROID_ARCHS=("armeabi-v7a" "arm64-v8a" "x86" "x86_64")
fi

for ARCH in "${ANDROID_ARCHS[@]}"; do
  SRC_DIR="build_android_${ARCH}/PluginSource"
  SRC_DIR2="vlc-android/libvlcjni/libvlc/jni/libs/${ARCH}"
  DEST_DIR="Plugins/Android/libs/${ARCH}"
  mkdir -p "$DEST_DIR"
  cp "$SRC_DIR/libVLCUnityPlugin.so" "$DEST_DIR/"
  cp "$SRC_DIR2/libvlc.so" "$DEST_DIR/"
done

cp vlc-android/libvlcjni/libvlc/build/intermediates/aar_main_jar/release/syncReleaseLibJars/classes.jar Plugins/Android/libs/armeabi-v7a
