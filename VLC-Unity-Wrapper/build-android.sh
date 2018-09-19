#! /bin/bash

ARCH=armeabi-v7a
SHELL=0
RELEASE=0

while getopts "rsa:" opt; do
	case "$opt" in
		r)
			RELEASE=1
			;;
		a)
			ARCH=$OPTARG
			;;
		s)
			SHELL=1
			;;
	esac
done

git submodule update --recursive
cd vlc
git checkout tags/3.0.4

echo "Applying opengl patches"

git am ../patches/*.patch

cd ..

DOCKER_REP="registry.videolan.org:5000/vlc-debian-android"
DOCKER_OPT="--mount type=bind,source=$(pwd)/vlc-android,target=/vlc-android/ \
			--mount type=bind,source=$(pwd)/vlc,target=/vlc/ \
			--mount type=bind,source=$(pwd),target=/unity/ \
           -e ANDROID_SDK=/sdk/android-sdk-linux/ \
           -e ANDROID_NDK=/sdk/android-ndk/ \
            "

if [ "$SHELL" = 1 ]
then
	docker run -ti -w /unity/ ${DOCKER_OPT} ${DOCKER_REP} /bin/bash
	exit 0
fi

echo "patch vlc-android"

#we need to package wdummy, not needed anymode as we use android window
#sed -e "/\.dummy/ d" -i ./vlc-android/compile-libvlc.sh
ln -s /vlc ./vlc-android/vlc

echo "BUILD libvlc for ${TRIPLET}"

if [ "$RELEASE" = 1 ]; then
	VLC_ANDROID_EXTRA_OPTS="${VLC_ANDROID_EXTRA_OPTS} --release"
fi

docker run -t -w /vlc-android/ ${DOCKER_OPT} ${DOCKER_REP} ./compile-libvlc.sh -a ${ARCH} --no-ml ${VLC_ANDROID_EXTRA_OPTS}

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

echo "BUILD unity-android for ${TRIPLET}"

#UNITY_BUILD_DIR=build-android-${ARCH}
#mkdir -p ./${UNITY_BUILD_DIR}
#docker run -t -w /unity/${UNITY_BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} \
#	   cmake .. \
#	   -DCMAKE_SYSTEM_NAME=Android \
#	   -DCMAKE_ANDROID_NDK=/sdk/android-ndk/ \
#	   -DCMAKE_PREFIX_PATH=/vlc-android/install-${ARCH} \
#	   -DCMAKE_ANDROID_STL_TYPE=c++_static \
#	   -DCMAKE_SYSTEM_VERSION=21
#
#docker run -t -w /unity/${UNITY_BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} make
#
#mkdir -p Plugins/Android/libs/${ARCH}
#cp ./${UNITY_BUILD_DIR}/libVlcUnityWrapper.so Plugins/Android/libs/${ARCH}
#cp ./vlc-android/libvlc/jni/libs/${ARCH}/libvlcjni.so Plugins/Android/libs/${ARCH}

#docker run -t -w /unity/android-wrapper ${DOCKER_OPT} ${DOCKER_REP} \
#	   ./gradlew \
#	   --gradle-user-home /unity/android-wrapper

docker run -t -w /unity/android-wrapper ${DOCKER_OPT} ${DOCKER_REP} \
	   ./gradlew \
	   --gradle-user-home /unity/android-wrapper/gradle-home \
	   build

if [ "$RELEASE" = 1 ]; then
	AAR_VERSION="release"
else
	AAR_VERSION="debug"
fi

mkdir -p Plugins/Android/libs/${ARCH}
cp android-wrapper/vlcunitylib/build/outputs/aar/vlcunitylib-${AAR_VERSION}.aar Plugins/Android/libs/${ARCH}/vlcunitylib.aar
