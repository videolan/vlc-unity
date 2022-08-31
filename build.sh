#! /bin/bash

set -e

RELEASE=1

while [ $# -gt 0 ]; do
    case $1 in
        help|--help|-h)
            echo "Use -a to set the ARCH:"
            echo "  ARM:     (armeabi-v7a|arm)"
            echo "  ARM64:   (arm64-v8a|arm64)"
            echo "  x64:     x64, x86_64"
            echo "  x86:     x86, i686"
            echo " "
            echo "Use -p to set the PLATFORM:"
            echo "  Windows:       win"
            echo "  Android:       android"
            echo " "    
            echo "Use --release to build in release mode"
            echo " "
            echo "Use --trial to build the trial version"
            exit 0
            ;;
        a|-a)
            ARCH=$2
            shift
            ;;
        p|-p)
            PLATFORM=$2
            shift
            ;;
        t|-t|--trial)
            TRIAL=1
            ;;
        -r|release|--release)
            RELEASE=0
            ;;
        *)
            diagnostic "$0: Invalid option '$1'."
            diagnostic "$0: Try --help for more information."
            exit 1
            ;;
    esac
    shift
done

if [ -z $PLATFORM ] && [ -z $ARCH ]
then
    echo "Platform and Arch undefined... Building for Windows x64."
    PLATFORM=win
    ARCH=x86_64
fi

echo "Building for OS '$PLATFORM' with target arch '$ARCH'"
if [ "$TRIAL" == 1 ]; then
echo "TRIAL version build enabled"
fi

OUTPUT="../Assets/VLCUnity/Plugins/$ARCH"

if [ "$PLATFORM" = "android" ]; then
    cd PluginSource
    if [ ! -d "include" ]; then
        wget -O headers.zip https://code.videolan.org/videolan/vlc/-/archive/master/vlc-master.zip?path=include
        unzip headers.zip
        mv vlc-master-include/include/ .
        rm -rf vlc-master-include headers.zip
    fi
    cd jni
    /sdk/android-ndk-r25/ndk-build NDK_DEBUG=$RELEASE APP_ABI=$ARCH TRIAL=$TRIAL
    cd ../..
    mv -f PluginSource/libs/$ARCH/libVLCUnityPlugin.so Assets/VLCUnity/Plugins/Android/libs/$ARCH/
    rm -rf PluginSource/libs PluginSource/obj
else
    cd PluginSource && make clean && make PLATFORM=$PLATFORM ARCH=$ARCH TRIAL=$TRIAL
    mv VLCUnityPlugin.{dll,pdb} $OUTPUT -f
fi
