#!/bin/sh
# Integrate Nikon machines and functional devices, then use upstream's native build.
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
if [ -e "$src/hw/nikon" ] || [ -e "$src/include/hw/nikon" ] || [ -n "$(git -C "$src" status --porcelain)" ]; then
    echo "QEMU source must be clean and not already integrated" >&2
    exit 1
fi
mkdir -p "$2"
build=$(CDPATH= cd -- "$2" && pwd)
if [ -n "$(ls -A "$build")" ]; then
    echo "build directory must be empty" >&2
    exit 1
fi
git -C "$src" apply --check "$repo/scripts/nikon-registration.patch"
git -C "$src" apply "$repo/scripts/nikon-registration.patch"
mkdir -p "$src/hw/nikon/machines" "$src/hw/nikon/devices" \
         "$src/include/hw/nikon"
cp "$repo/hw/nikon/Kconfig" "$repo/hw/nikon/meson.build" "$src/hw/nikon/"
cp "$repo/hw/nikon/machines/"*.c "$src/hw/nikon/machines/"
cp "$repo/hw/nikon/devices/"*.c "$src/hw/nikon/devices/"
cp "$repo/include/hw/nikon/"*.h "$src/include/hw/nikon/"
cd "$build"
"$src/configure" --target-list=arm-softmmu --disable-docs --disable-werror
ninja qemu-system-arm
# Compiling a board is insufficient: target interfaces must expose it too.
"$build/qemu-system-arm" -M help | grep '^z7ii-mas-diagnostic[[:space:]]'
