#! /bin/bash

version="b5d43240"
date="20200512-0453"
sourceLocation="Assets/PluginSource"
downloadUrl="https://artifacts.videolan.org/vlc/nightly-win64-llvm/$date/vlc-4.0.0-dev-win64-$version.7z"

rm ./build.7z
rm -rf ./build
rm -rf $sourceLocation/include
rm -rf $sourceLocation/sdk
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/libvlc.dll
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/libvlccore.dll
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/hrtfs
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/locale
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/lua
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/plugins

curl -Lsfo build.7z $downloadUrl
7z x build.7z -o./build

cp -R ./build/vlc-4.0.0-dev/{libvlc.dll,libvlccore.dll,hrtfs,locale,lua,plugins} Assets/VLC-Unity-Windows/Plugins/x86_64
mkdir -p $sourceLocation/sdk/lib/
cp -r ./build/vlc-4.0.0-dev/sdk/lib/ $sourceLocation/sdk/
cp -r ./build/vlc-4.0.0-dev/sdk/include $sourceLocation/include

# rm ./build.7z
# rm -rf ./build