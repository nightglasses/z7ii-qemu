#!/bin/sh
# Integrate the one diagnostic machine, then use upstream's native build.
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: $0 <pinned-qemu-source> <empty-build-directory>" >&2
    exit 2
fi
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(CDPATH= cd -- "$1" && pwd)
ref=4fc49f46dc95d4a27de2509e7fceb2931e91faeb
if [ "$(git -C "$src" rev-parse HEAD)" != "$ref" ]; then
    echo "QEMU source must be at pinned commit $ref" >&2
    exit 1
fi
# Do not overwrite an existing integration or anyone else's source changes.
if [ -e "$src/hw/arm/z7ii.c" ] || [ -n "$(git -C "$src" status --porcelain)" ]; then
    echo "QEMU source must be clean and not already integrated" >&2
    exit 1
fi
mkdir -p "$2"
build=$(CDPATH= cd -- "$2" && pwd)
if [ -n "$(ls -A "$build")" ]; then
    echo "build directory must be empty" >&2
    exit 1
fi
git -C "$src" apply --check "$repo/scripts/z7ii-registration.patch"
git -C "$src" apply "$repo/scripts/z7ii-registration.patch"
cp "$repo/hw/arm/z7ii.c" "$repo/hw/arm/z7ii.h" \
   "$repo/hw/arm/z7ii-timestamp.c" "$repo/hw/arm/z7ii-sdi.c" \
   "$repo/hw/arm/z7ii-mxic.c" "$repo/hw/arm/z7ii-fill.c" \
   "$repo/hw/arm/z7ii-usio.c" "$repo/hw/arm/z7ii-sysconfig.c" \
   "$repo/hw/arm/z7ii-gpio.c" "$repo/hw/arm/z7ii-mxic-config.c" \
   "$repo/hw/arm/z7ii-spi.c" "$repo/hw/arm/z7ii-exiu.c" \
   "$repo/hw/arm/z7ii-hdmac.c" "$repo/hw/arm/z7ii-i2c.c" \
   "$repo/hw/arm/z7ii-audio.c" "$repo/hw/arm/z7ii-timing.c" \
   "$repo/hw/arm/z7ii-copy.c" "$repo/hw/arm/z7ii-bridge.c" "$src/hw/arm/"
cd "$build"
"$src/configure" --target-list=arm-softmmu --disable-docs --disable-werror
ninja qemu-system-arm
# Compiling a board is insufficient: target interfaces must expose it too.
"$build/qemu-system-arm" -M help | grep '^z7ii-mas-diagnostic[[:space:]]'
