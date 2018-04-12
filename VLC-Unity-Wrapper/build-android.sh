#! /bin/bash

ARCH=armeabi-v7a

while getopts "a:" opt; do
	case "$opt" in
		a)
			ARCH=$OPTARG
			;;
	esac
done

DOCKER_REP="registry.videolan.org:5000/vlc-debian-android"
DOCKER_OPT="--mount type=bind,source=$(pwd)/vlc-android,target=/vlc-android/ \
			--mount type=bind,source=$(pwd)/vlc,target=/vlc-android/vlc/ \
			--mount type=bind,source=$(pwd),target=/unity/ \
           -e ANDROID_SDK=/sdk/android-sdk-linux/ \
           -e ANDROID_NDK=/sdk/android-ndk/ \
            "

docker run -t -w /vlc-android/ ${DOCKER_OPT} ${DOCKER_REP} ./compile-libvlc.sh -a armeabi-v7a --no-ml

INST_DIR=./vlc-android/install-${ARCH}
mkdir -p ${INST_DIR}/include
rsync -Pa ./vlc/include/vlc ${INST_DIR}/include
mkdir -p ${INST_DIR}/lib
rsync -Pa ./vlc-android/libvlc/jni/libs/${ARCH}/libvlcjni.so ${INST_DIR}/lib/
mkdir -p ${INST_DIR}/lib/pkgconfig
cat <<EOF > ${INST_DIR}/lib/pkgconfig/libvlc.pc
Name: LibVLC control API
Description: VLC media player external control library
Version: 3.0.2
Cflags: -I/vlc-android/install-${ARCH}/include
Libs: -L/vlc-android/install-${ARCH}/lib -lvlcjni
EOF

UNITY_BUILD_DIR=build-android-${ARCH}
mkdir -p ./${UNITY_BUILD_DIR}
docker run -t -w /unity/${UNITY_BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} \
	   cmake .. \
	   -DCMAKE_SYSTEM_NAME=Android \
	   -DCMAKE_ANDROID_NDK=/sdk/android-ndk/ \
	   -DCMAKE_PREFIX_PATH=/vlc-android/install-${ARCH} \
	   -DCMAKE_ANDROID_STL_TYPE=c++_static \
	   -DCMAKE_SYSTEM_VERSION=21

docker run -t -w /unity/${UNITY_BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} make

mkdir -p Plugins/Android/libs/${ARCH}
cp ./${UNITY_BUILD_DIR}/libVlcUnityWrapper.so Plugins/Android/libs/${ARCH}
cp ./vlc-android/libvlc/jni/libs/${ARCH}/libvlcjni.so Plugins/Android/libs/${ARCH}
