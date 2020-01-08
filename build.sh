#! /bin/bash

set -e

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
        -r|release|--release)
            RELEASE=1
            ;;
        *)
            diagnostic "$0: Invalid option '$1'."
            diagnostic "$0: Try --help for more information."
            exit 1
            ;;
    esac
    shift
done

echo "Building for OS '$PLATFORM' with target arch '$ARCH'"

OUTPUT="../$ARCH"

cd Assets/VLC-Unity-Windows/Plugins/Source && make clean && make PLATFORM=$PLATFORM ARCH=$ARCH
mv VLCUnityPlugin.{dll,pdb} $OUTPUT -f