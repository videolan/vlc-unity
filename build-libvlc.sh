#!/bin/bash
# Build libvlc from source.
# Runs inside the VLC build container (locally via Docker, or natively in CI).
#
# Usage:
#   ./build-libvlc.sh <TRIPLET> <ARCH>
#   e.g. ./build-libvlc.sh x86_64-linux-gnu x86_64
#
# Output: ./linux-install/  (relative to the working directory)
set -ex

TRIPLET="${1:?Usage: $0 <TRIPLET> <ARCH>}"
ARCH="${2:?Usage: $0 <TRIPLET> <ARCH>}"
export NCPU=$(getconf _NPROCESSORS_ONLN)

# Clone VLC if not already present
if [ ! -d vlc ]; then
    git clone --depth 1 https://code.videolan.org/videolan/vlc
fi
cd vlc

# Build extras/tools if not already built
if [ ! -f extras/tools/.buildmeson ]; then
    cd extras/tools && ./bootstrap && make -j$NCPU --output-sync=recurse
    cd ../..
fi
export PATH="$(pwd)/extras/tools/build/bin:$PATH"

# Build contribs from source — ignore system packages so all deps are
# built as static libs, making VLC plugins self-contained.
export PKG_CONFIG_LIBDIR=""
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR=""
# Expose selected system pkg-config files that contrib-built dependencies rely on
# during their own configure/build steps (for example Vulkan-Loader needs X11/XCB).
mkdir -p contrib/${TRIPLET}/lib/pkgconfig
src="/usr/lib/${TRIPLET}/pkgconfig/x11.pc"
if [ -f "$src" ]; then
    cp "$src" "contrib/${TRIPLET}/lib/pkgconfig/x11.pc"
    sed -i '/^Requires:/d' "contrib/${TRIPLET}/lib/pkgconfig/x11.pc"
fi
for pc in gl egl glesv2 libunwind libunwind-generic lcms2 \
          x11-xcb xau xdmcp xcb xext xfixes xrender xrandr; do
    src="/usr/lib/${TRIPLET}/pkgconfig/${pc}.pc"
    [ -f "$src" ] || src="/usr/share/pkgconfig/${pc}.pc"
    [ -f "$src" ] && ln -sf "$src" "contrib/${TRIPLET}/lib/pkgconfig/" || true
done
for pc in xproto kbproto; do
    src="/usr/share/pkgconfig/${pc}.pc"
    [ -f "$src" ] && ln -sf "$src" "contrib/${TRIPLET}/lib/pkgconfig/" || true
done

mkdir -p contrib/contrib-${TRIPLET} && cd contrib/contrib-${TRIPLET}
if [ ! -f Makefile ]; then
    ../bootstrap --disable-x264 --disable-x265 --disable-dvdread --disable-dvdnav \
        --enable-glslang --enable-libplacebo \
        --enable-vulkan-headers --enable-vulkan-loader
fi

make list 2>&1 | grep -A5 "Distribution-provided\|Automatically selected" || true

make -j$NCPU --output-sync=recurse fetch
# Force-build libplacebo before the main parallel build.
# The contrib "all" target may finish with unrelated errors before this path
# completes, leaving libplacebo absent even though it is otherwise buildable.
# Build matching contrib Vulkan headers first so vulkan-loader does not pick
# newer system headers with incompatible generated entrypoint signatures.
# Building it up front also guarantees that we use the contrib
# glslang static libraries instead of whatever the container happens to expose.
make .vulkan-headers || true
make .glslang || true
make .libplacebo || true
make -j$NCPU --output-sync=recurse -k || true

# Verify critical contribs were built as static libs
ls ../${TRIPLET}/lib/libavcodec.a  || { echo "ERROR: FFmpeg static libs missing"; exit 1; }
ls ../${TRIPLET}/lib/libplacebo.a  || { echo "ERROR: libplacebo static lib missing"; exit 1; }

mkdir -p ../${TRIPLET}/lib/pkgconfig

# Replace system xau/xcb pkg-config metadata with contrib versions so VLC
# links against the matching static libraries and their dependencies.
src=$(find . -path "*/libxau/vlc_build/xau.pc" 2>/dev/null | head -1)
[ -n "$src" ] && rm -f "../${TRIPLET}/lib/pkgconfig/xau.pc" && cp "$src" "../${TRIPLET}/lib/pkgconfig/xau.pc" || true
for pkg in xcb xcb-composite xcb-dbe xcb-dri3 xcb-keysyms xcb-present \
           xcb-randr xcb-record xcb-render xcb-shape xcb-shm xcb-xfixes xcb-xkb; do
    src=$(find . -path "*/libxcb/vlc_build/${pkg}.pc" 2>/dev/null | head -1)
    dest="../${TRIPLET}/lib/pkgconfig/${pkg}.pc"
    [ -n "$src" ] && [ -f "$src" ] && rm -f "$dest" && cp "$src" "$dest" || true
done

cd ../..

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

# Flatten only the top-level libvlc/libvlccore runtime libs and create SONAME
# symlinks for them. Keep the lib/vlc helper libraries versioned: some VLC
# modules depend on their real filename targets (for example
# libvlc_xcb_events.so.0.0.0).
cd linux-install/lib
for f in libvlc.so.*.*.* libvlccore.so.*.*.*; do
  [ -f "$f" ] || continue
  base=$(echo "$f" | sed 's/\.so\..*/.so/')
  soname=$(objdump -p "$f" 2>/dev/null | awk '/SONAME/{print $2}')
  mv "$f" "$base"
  [ -n "$soname" ] && [ "$soname" != "$base" ] && ln -sf "$base" "$soname"
done
find . -maxdepth 1 -type f \( -name "libvlc.so.*" -o -name "libvlccore.so.*" \) -delete 2>/dev/null || true
cd ../..

# Strip the lib/vlc/ tree down to what's actually used at runtime.
# libtool installs a lot of link-time-only artifacts (.la descriptors,
# .a static archives). Keep the versioned .so files there: VLC modules may
# reference them by exact SONAME at runtime.
find linux-install/lib/vlc \( -name "*.la" -o -name "*.a" \) -delete
find linux-install/lib -type f \( -name "*.so" -o -name "*.so.*" \) -exec strip --strip-unneeded {} +

echo "=== Build complete ==="
ls -la linux-install/lib/libvlc* 2>/dev/null || true
echo "--- lib/vlc/ ---"
ls linux-install/lib/vlc/ 2>/dev/null || true
