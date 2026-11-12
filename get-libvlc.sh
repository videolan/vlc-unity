#!/bin/bash
# Download and unpack prebuilt LibVLC.
#   ./get-libvlc.sh --resolve     print the version to use
#   ./get-libvlc.sh PACKAGE...    e.g. libvlc-linux-x86_64
# Needs LIBVLC_BUILDS_TOKEN. LIBVLC_BUILDS_VERSION defaults to latest.
set -euo pipefail

API=https://gitlab.com/api/v4/projects/videolabs%2Fprototypes%2Flibvlc-builds/packages/generic/libvlc

if [ -z "${LIBVLC_BUILDS_TOKEN:-}" ]; then
    echo "LIBVLC_BUILDS_TOKEN is not set" >&2
    exit 1
fi

# token on stdin, not on the command line
fetch() {
    printf 'header = "DEPLOY-TOKEN: %s"\n' "$LIBVLC_BUILDS_TOKEN" \
        | curl --config - --fail --silent --show-error --location --retry 3 "$@"
}

resolve() {
    local version=${LIBVLC_BUILDS_VERSION:-latest}
    if [ "$version" = latest ]; then
        version=$(fetch "$API/latest/VERSION")
    fi
    if ! [[ $version =~ ^[0-9]{8}-[0-9a-f]{8}-[0-9]+$ ]]; then
        echo "unexpected version '$version'" >&2
        return 1
    fi
    echo "$version"
}

if [ "${1:-}" = --resolve ]; then
    resolve
    exit
fi
if [ $# = 0 ]; then
    echo "usage: $0 --resolve | PACKAGE..." >&2
    exit 1
fi

# point the .pc files at the unpacked tree
relocate_pc() {
    local pc=$1 dir root
    dir=$(cd "$(dirname "$pc")" && pwd)
    case $pc in
    vlc-android/libvlcjni/libvlc/jni/pkgconfig/*/libvlc.pc)
        root=$PWD/vlc-android/libvlcjni
        sed -i -e "s|^prefix=.*|prefix=$root/libvlc/jni|" \
            -e "s|^libdir=.*|libdir=$root/libvlc/jni/libs/${dir##*/}|" \
            -e "s|^includedir=.*|includedir=$root/vlc/include|" "$pc"
        ;;
    */lib/pkgconfig/*.pc)
        sed -i "s|^prefix=.*|prefix=$(dirname "$(dirname "$dir")")|" "$pc"
        ;;
    esac
}

version=$(resolve)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fetch -o "$tmp/SHA256SUMS" "$API/$version/SHA256SUMS"
fetch -o "$tmp/manifest.json" "$API/$version/manifest.json"
echo "libvlc $version, VLC $(grep -o '"vlc": {[^}]*}' "$tmp/manifest.json")"

for package in "$@"; do
    file=$package.tar.gz
    sum=$(awk -v f="$file" '{ n = $2; sub(/^\*/, "", n) } n == f' "$tmp/SHA256SUMS")
    if [ -z "$sum" ]; then
        echo "$file is not in $version" >&2
        exit 1
    fi
    fetch -o "$tmp/$file" "$API/$version/$file"
    (cd "$tmp" && echo "$sum" | sha256sum --check --strict -)
    tar -xzf "$tmp/$file" --no-same-owner
    tar -tzf "$tmp/$file" | { grep '\.pc$' || true; } | while read -r pc; do
        relocate_pc "$pc"
    done
    rm "$tmp/$file"
done
