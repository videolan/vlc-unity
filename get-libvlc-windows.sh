#! /bin/bash
sourceLocation="Assets/PluginSource"

rm ./build.7z
rm -rf ./build
rm -rf $sourceLocation/sdk
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/libvlc.dll
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/libvlccore.dll
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/hrtfs
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/locale
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/lua
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/plugins

7z x vlc-4.0.0-dev-win64.7z -o./build

cp -R ./build/vlc-4.0.0-dev/{libvlc.dll,libvlccore.dll,hrtfs,locale,lua,plugins} Assets/VLC-Unity-Windows/Plugins/x86_64
rm -rf Assets/VLC-Unity-Windows/Plugins/x86_64/lua/http # contains unnecessary js files which make the local Unity Store validator fail
cp -r ./build/vlc-4.0.0-dev/sdk/ $sourceLocation/