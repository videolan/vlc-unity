#!/bin/bash
# Build libvlc locally using the same Docker image as CI.
# Output: ./linux-x86_64/linux-install/
#
# Reuses an existing container if available (docker start + exec),
# otherwise creates a new one. This avoids rebuilding from scratch.
set -e

ARCH=x86_64
TRIPLET=x86_64-linux-gnu
IMAGE=registry.videolan.org/vlc-debian-unstable:202601091456
CONTAINER_NAME=vlc-build-linux
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Building libvlc in Docker ($IMAGE) ==="

# Reuse existing container or create a new one
if docker inspect "$CONTAINER_NAME" &>/dev/null; then
    echo "Reusing existing container '$CONTAINER_NAME'"
    docker start "$CONTAINER_NAME"
else
    echo "Creating new container '$CONTAINER_NAME'"
    docker create --name "$CONTAINER_NAME" --memory=8g "$IMAGE" sleep infinity
    docker start "$CONTAINER_NAME"
fi

# Copy the build script into the container and run it
docker cp "$SCRIPT_DIR/build-libvlc.sh" "$CONTAINER_NAME:/tmp/build-libvlc.sh"
docker exec "$CONTAINER_NAME" \
    bash -c "cd /tmp && bash build-libvlc.sh '$TRIPLET' '$ARCH'"

echo "=== Copying output from container ==="
mkdir -p "linux-${ARCH}/linux-install"
docker cp "$CONTAINER_NAME:/tmp/vlc/linux-install/." "linux-${ARCH}/linux-install/"

echo "=== Done! Output in ./linux-${ARCH}/linux-install/ ==="
ls -la "linux-${ARCH}/linux-install/lib/" | head -20
echo "Container '$CONTAINER_NAME' kept alive for reuse."
