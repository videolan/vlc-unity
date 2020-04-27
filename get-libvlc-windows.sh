#! /bin/bash

version="4.0.0-20200317-0757"
sourceLocation="Assets/PluginSource"
downloadUrl="https://nightlies.videolan.org/build/win64-llvm/vlc-$version/vlc-$version-dev-win64.7z"

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