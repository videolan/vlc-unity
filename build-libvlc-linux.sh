#!/bin/bash
# Build libvlc locally using the same Docker image as CI.
# Output: ./linux-x86_64/linux-install/
#
# Reuses an existing container if available (docker start + exec),
# otherwise creates a new one. This avoids rebuilding from scratch.
set -e

ARCH=x86_64
TRIPLET=x86_64-linux-gnu
IMAGE=registry.videolan.org/vlc-debian-unstable:20241112155431
CONTAINER_NAME=vlc-build-linux

# Build script that runs inside the container
BUILD_SCRIPT='
set -ex
export NCPU=$(getconf _NPROCESSORS_ONLN)
export TRIPLET='"$TRIPLET"'
export ARCH='"$ARCH"'

cd /tmp

# Clone VLC if not already present
if [ ! -d vlc ]; then
    git clone --depth 1 https://code.videolan.org/videolan/vlc
fi
cd vlc

# Build extras/tools if not already built
if [ ! -f extras/tools/.buildmeson ]; then
    cd extras/tools && ./bootstrap && make -j$NCPU --output-sync=recurse
    cd ../..
else
    cd extras/tools
    cd ../..
fi
export PATH="/tmp/vlc/extras/tools/build/bin:$PATH"

# Build contribs from source — ignore system packages so all deps are
# built as static libs, making VLC plugins self-contained.
export PKG_CONFIG_LIBDIR=""
export PKG_CONFIG_PATH=""
# Symlink system x11.pc into contrib prefix so vulkan-loader cmake can find it
mkdir -p contrib/${TRIPLET}/lib/pkgconfig
for pc in x11 xau xdmcp xcb xext xfixes xrender xrandr; do
    src="/usr/lib/${TRIPLET}/pkgconfig/${pc}.pc"
    [ -f "$src" ] && ln -sf "$src" "contrib/${TRIPLET}/lib/pkgconfig/" 2>/dev/null || true
done
for pc in xproto kbproto; do
    src="/usr/share/pkgconfig/${pc}.pc"
    [ -f "$src" ] && ln -sf "$src" "contrib/${TRIPLET}/lib/pkgconfig/" 2>/dev/null || true
done
mkdir -p contrib/contrib-${TRIPLET} && cd contrib/contrib-${TRIPLET}
if [ ! -f Makefile ]; then
    ../bootstrap --disable-x264 --disable-x265 --disable-dvdread --disable-dvdnav
fi

make list 2>&1 | grep -A5 "Distribution-provided\|Automatically selected"

make -j$NCPU --output-sync=recurse fetch
make -j$NCPU --output-sync=recurse -k || true

# Verify critical contribs were built
echo "=== Verifying contrib builds ==="
ls -la ../${TRIPLET}/lib/libavcodec.a || echo "WARNING: No libavcodec.a!"
ls -la ../${TRIPLET}/lib/libplacebo.a 2>/dev/null || echo "WARNING: No libplacebo.a!"

# Copy missing .pc files from contrib build dirs into the contrib prefix.
# The contrib build does not always run make install for every package,
# so .pc files may only exist inside per-package build directories.
find . -path "*/vlc_build/*.pc" -not -path "*/doc/*" -not -name "*-uninstalled*" | while read pc; do
    name=$(basename "$pc")
    dest="../${TRIPLET}/lib/pkgconfig/$name"
    [ ! -f "$dest" ] && cp "$pc" "$dest"
done

# Replace system-symlinked xcb .pc files with contrib-built ones so that
# Xau/Xdmcp are listed as public deps and get linked correctly.
for pkg in xcb xcb-composite xcb-dbe xcb-dri3 xcb-keysyms xcb-present \
           xcb-randr xcb-record xcb-render xcb-shape xcb-shm xcb-xfixes xcb-xkb; do
    src=$(find . -path "*/libxcb/vlc_build/${pkg}.pc" 2>/dev/null | head -1)
    dest="../${TRIPLET}/lib/pkgconfig/${pkg}.pc"
    [ -n "$src" ] && [ -f "$src" ] && rm -f "$dest" && cp "$src" "$dest"
done
# Replace system xau.pc with contrib version
src=$(find . -path "*/libxau/vlc_build/xau.pc" 2>/dev/null | head -1)
[ -n "$src" ] && rm -f "../${TRIPLET}/lib/pkgconfig/xau.pc" && cp "$src" "../${TRIPLET}/lib/pkgconfig/xau.pc"

# Symlink system .pc files for packages not built as contribs
# but needed by configure (GL/EGL, libunwind, lcms2).
for pc in gl egl glesv2 libunwind libunwind-generic lcms2; do
    src="/usr/lib/${TRIPLET}/pkgconfig/${pc}.pc"
    [ -f "$src" ] && ln -sf "$src" "../${TRIPLET}/lib/pkgconfig/"
done

cd ../..

# Patch libplacebo usage to allow software rasterizers (e.g. llvmpipe)
# so the GL output works even without GPU hardware acceleration.
sed -i "s/add_bool(\"gl-allow-sw\", false,/add_bool(\"gl-allow-sw\", true,/" \
    modules/video_output/libplacebo/instance_opengl.c
sed -i "/.make_current = NULL,/a\\        .allow_software = true," \
    modules/video_output/opengl/sampler.c
sed -i "/struct pl_opengl_params opengl_params = {/a\\        .allow_software = true," \
    modules/video_output/opengl/pl_scale.c

# Bootstrap and configure VLC
if [ ! -f configure ]; then
    ./bootstrap
fi
./configure \
  --prefix=$(pwd)/linux-install \
  --disable-vlc \
  --disable-x264 \
  --disable-x26410b \
  --disable-x265 \
  --disable-dvdread \
  --disable-dvdnav \
  --disable-qt \
  --disable-skins2 \
  --disable-ncurses \
  --disable-lirc \
  --disable-lua \
  --disable-dbus \
  --disable-avahi \
  --disable-mtp \
  --disable-notify \
  --disable-sout

# Build and install
make -j$NCPU
make install

# Flatten versioned libs and create soname symlinks
cd linux-install/lib
for f in *.so.*.*.*; do
  [ -f "$f" ] || continue
  base=$(echo "$f" | sed "s/\.so\..*/\.so/")
  soname=$(objdump -p "$f" 2>/dev/null | awk "/SONAME/{print \$2}")
  mv "$f" "$base"
  [ -n "$soname" ] && [ "$soname" != "$base" ] && ln -sf "$base" "$soname"
done
find . -maxdepth 1 -name "*.so.*" -type f -delete 2>/dev/null || true

echo "=== Build complete ==="
ls -la /tmp/vlc/linux-install/lib/libvlc* 2>/dev/null || true
echo "=== Checking libavcodec_plugin deps ==="
ldd /tmp/vlc/linux-install/lib/vlc/plugins/codec/libavcodec_plugin.so 2>/dev/null || true
echo "=== Checking libgl_plugin deps ==="
ldd /tmp/vlc/linux-install/lib/vlc/plugins/video_output/libgl_plugin.so 2>/dev/null || true
'

echo "=== Building libvlc in Docker ($IMAGE) ==="

# Reuse existing container or create a new one
if docker inspect "$CONTAINER_NAME" &>/dev/null; then
    echo "Reusing existing container '$CONTAINER_NAME'"
    docker start "$CONTAINER_NAME"
    docker exec "$CONTAINER_NAME" bash -c "$BUILD_SCRIPT"
else
    echo "Creating new container '$CONTAINER_NAME'"
    docker create --name "$CONTAINER_NAME" --memory=8g "$IMAGE" sleep infinity
    docker start "$CONTAINER_NAME"
    docker exec "$CONTAINER_NAME" bash -c "$BUILD_SCRIPT"
fi

echo "=== Copying output from container ==="
mkdir -p "linux-${ARCH}/linux-install"
docker cp "$CONTAINER_NAME:/tmp/vlc/linux-install/." "linux-${ARCH}/linux-install/"

echo "=== Done! Output in ./linux-${ARCH}/linux-install/ ==="
ls -la "linux-${ARCH}/linux-install/lib/" | head -20
echo "Container '$CONTAINER_NAME' kept alive for reuse."
