#! /bin/bash

ARCH=x86_64
HOST=win64
TRIPLET=x86_64-w64-mingw32
VERSION=3.0.2

while getopts "sa:" opt; do
    case "$opt" in
    a)
        if [ "x$OPTARG" = "xx86_64" -o "x$OPTARG" = "xwin64" -o "x$OPTARG" = "xw64" ]
		then
			ARCH=x86_64
			HOST=win64
			TRIPLET=x86_64-w64-mingw32
		elif [ "x$OPTARG" = "xi686" -o "x$OPTARG" = "xwin32" -o "x$OPTARG" = "xw32" ]
		then
			ARCH=i686
			HOST=win32
			TRIPLET=i686-w64-mingw32
		fi
        ;;
	s)
		SHELL="yes"
		;;
    esac
done


JOBS=$(getconf _NPROCESSORS_ONLN 2>&1)

DOCKER_REP="registry.videolan.org:5000/vlc-debian-${HOST}"
DOCKER_OPT="--mount type=bind,source=$(pwd)/vlc,target=/vlc/ \
     --mount type=bind,source=$(pwd)/,target=/unity/ \
    -e PATH=/vlc/extras/tools/build/bin/:/opt/gcc-${TRIPLET}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    -e USE_FFMPEG=1 \
    -e PKG_CONFIG_LIBDIR=/vlc/contrib/${TRIPLET}/lib/pkgconfig/ \
    "

if [ "$SHELL" = "yes" ]
then
	docker run -ti -w /unity/ ${DOCKER_OPT} ${DOCKER_REP} /bin/bash
	exit 0
fi


echo "BUILD extra tools"

docker run -t -w /vlc/extras/tools ${DOCKER_OPT} ${DOCKER_REP} ./bootstrap
docker run -t -w /vlc/extras/tools ${DOCKER_OPT} ${DOCKER_REP} make -j$JOBS

echo "BUILD contribs for ${TRIPLET}"

CONTRIB_BUILD_DIR=vlc/contrib/contrib-${TRIPLET}
mkdir -p ./${CONTRIB_BUILD_DIR}
docker run -t -w /${CONTRIB_BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} ../bootstrap \
    --host=${TRIPLET} \
    --disable-qt \
    --disable-qtsvg

docker run -t -w /${CONTRIB_BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} make -j$JOBS

BUILD_DIR=vlc/build-${TRIPLET}

if [ ! -e ./${BUILD_DIR}/config.h ]; then
mkdir -p ./${BUILD_DIR}
docker run -t -w /${BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} ../extras/package/win32/configure.sh \
    --host=$TRIPLET \
    --disable-nls \
    --disable-qt \
    --disable-update-check \
    --disable-skins2
fi

echo "BUILD libvlc for ${TRIPLET}"

docker run -t -w /${BUILD_DIR} ${DOCKER_OPT} ${DOCKER_REP} make package-win-strip -j$JOBS

echo "BUILD vlc-unity for ${TRIPLET}"

mkdir -p "build-${TRIPLET}"
docker run -t -w /unity/build-${TRIPLET} ${DOCKER_OPT} ${DOCKER_REP} cmake .. \
    -DCMAKE_PREFIX_PATH=/${BUILD_DIR}/_win32 \
    -DCMAKE_TOOLCHAIN_FILE=/${CONTRIB_BUILD_DIR}/toolchain.cmake

docker run -t -w /unity/build-${TRIPLET} ${DOCKER_OPT} ${DOCKER_REP} make

mkdir -p Plugins/${ARCH}
cp build-${TRIPLET}/VlcUnityWrapper.dll Plugins/${ARCH}/
mkdir -p libvlc/${ARCH}
rsync -Pa --include "*/" --include="*.dll" --exclude="*" vlc/build-${TRIPLET}/vlc-${VERSION}/ libvlc/${ARCH}/
