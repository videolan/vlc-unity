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
mkdir -p contrib/contrib-${TRIPLET} && cd contrib/contrib-${TRIPLET}
if [ ! -f Makefile ]; then
    ../bootstrap --disable-x264 --disable-x265 --disable-dvdread --disable-dvdnav
fi

make list 2>&1 | grep -A5 "Distribution-provided\|Automatically selected" || true

make -j$NCPU --output-sync=recurse fetch
# Force-build glslang from contrib source before the main parallel build.
# The Docker image may ship a newer system glslang that removed
# libOGLCompiler.a, but libplacebo still requires it.
make .glslang || true
make -j$NCPU --output-sync=recurse -k || true

# Verify critical contribs were built as static libs
ls ../${TRIPLET}/lib/libavcodec.a  || { echo "ERROR: FFmpeg static libs missing"; exit 1; }
ls ../${TRIPLET}/lib/libplacebo.a  || { echo "ERROR: libplacebo static lib missing"; exit 1; }

mkdir -p ../${TRIPLET}/lib/pkgconfig

# Replace system xau.pc with contrib version
src=$(find . -path "*/libxau/vlc_build/xau.pc" 2>/dev/null | head -1)
[ -n "$src" ] && rm -f "../${TRIPLET}/lib/pkgconfig/xau.pc" && cp "$src" "../${TRIPLET}/lib/pkgconfig/xau.pc" || true

# Symlink system .pc files for packages not built as contribs
# but needed by configure (GL/EGL, libunwind, lcms2).
for pc in gl egl glesv2 libunwind libunwind-generic lcms2; do
    src="/usr/lib/${TRIPLET}/pkgconfig/${pc}.pc"
    [ -f "$src" ] && ln -sf "$src" "../${TRIPLET}/lib/pkgconfig/" || true
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

# Fix RUNPATH: libtool bakes the --prefix install path into the libraries.
# Replace it with $ORIGIN so they find each other at runtime regardless of
# where they are deployed (Unity Editor, standalone build, etc.).
python3 -c "
import os, subprocess, re
libdir = 'linux-install/lib'
new = bytes([0x24]) + b'ORIGIN'
for name in os.listdir(libdir):
    path = os.path.join(libdir, name)
    if os.path.islink(path) or not os.path.isfile(path):
        continue
    if not (name.endswith('.so') or '.so.' in name):
        continue
    out = subprocess.check_output(['readelf', '-d', path], text=True, stderr=subprocess.DEVNULL)
    m = re.search(r'RUNPATH.*\[(.+)\]', out)
    if not m:
        continue
    old = m.group(1).encode()
    if old == new:
        continue
    data = open(path, 'rb').read()
    data = data.replace(old, new + b'\x00' * (len(old) - len(new)), 1)
    open(path, 'wb').write(data)
    print('Patched RUNPATH in ' + name)
"

# Flatten versioned libs and create soname symlinks
cd linux-install/lib
for f in *.so.*.*.*; do
  [ -f "$f" ] || continue
  base=$(echo "$f" | sed 's/\.so\..*/.so/')
  soname=$(objdump -p "$f" 2>/dev/null | awk '/SONAME/{print $2}')
  mv "$f" "$base"
  [ -n "$soname" ] && [ "$soname" != "$base" ] && ln -sf "$base" "$soname"
done
find . -maxdepth 2 -name "*.so.*" -type f -delete 2>/dev/null || true
cd ../..

# Strip the lib/vlc/ tree down to what's actually used at runtime.
# libtool installs a lot of link-time-only artifacts (.la descriptors,
# .a static archives) and duplicate symlinks for versioned sonames.
# Unity imports every file under Assets/ and gets confused by the meta
# churn, so we ship only the real .so files.
find linux-install/lib/vlc \( -name "*.la" -o -name "*.a" \) -delete

echo "=== Build complete ==="
ls -la linux-install/lib/libvlc* 2>/dev/null || true
echo "--- lib/vlc/ ---"
ls linux-install/lib/vlc/ 2>/dev/null || true
