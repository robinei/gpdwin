#!/bin/sh
# Build the GPD's 32-bit DXVK d3d9.dll on the DESKTOP (README.md): DXVK v2.4.1 + dxvk-gplasync-2.4-1.patch +
# 0001-gpd-d3d9-async-readback-and-wait-log.patch, in a clean Arch chroot with mingw-w64 (devtools, sudo).
# Output: ./x32/d3d9.dll and ./x64/d3d9.dll. The game installer (scripts/gamelib/core.py) copies the one matching
# the game's exe next to it (WINEDLLOVERRIDES d3d9=n); on the GPD they live in ~/Games/tools/dxvk/gpd/.
set -e
cd "$(dirname "$0")"
here=$PWD
CHROOT=/var/lib/archbuild/gpd-dxvk CONF=$here/../mesa/chroot-pacman.conf
MAKECONF=/usr/share/devtools/makepkg.conf.d/x86_64.conf
if [ ! -d "$CHROOT/root" ]; then
    sudo mkdir -p "$CHROOT"
    sudo mkarchroot -C "$CONF" -M "$MAKECONF" -c /var/cache/gpd-archbuild/pkg "$CHROOT/root" \
        base-devel mingw-w64-gcc meson ninja glslang git
fi
sudo rm -rf "$CHROOT/root/build"
sudo git clone -q --branch v2.4.1 --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/doitsujin/dxvk "$CHROOT/root/build"
sudo cp dxvk-gplasync-2.4-1.patch 0001-gpd-d3d9-async-readback-and-wait-log.patch "$CHROOT/root/build/"
sudo arch-nspawn -C "$CONF" "$CHROOT/root" sh -c 'cd /build && patch -Np1 -s < dxvk-gplasync-2.4-1.patch &&
    patch -Np1 -s < 0001-gpd-d3d9-async-readback-and-wait-log.patch &&
    meson setup b32 --cross-file build-win32.txt --buildtype release -Db_ndebug=true >/build/setup32.log &&
    ninja -C b32 src/d3d9/d3d9.dll >/build/ninja32.log && i686-w64-mingw32-strip b32/src/d3d9/d3d9.dll &&
    meson setup b64 --cross-file build-win64.txt --buildtype release -Db_ndebug=true >/build/setup64.log &&
    ninja -C b64 src/d3d9/d3d9.dll >/build/ninja64.log && x86_64-w64-mingw32-strip b64/src/d3d9/d3d9.dll'
mkdir -p x32 x64
sudo cat "$CHROOT/root/build/b32/src/d3d9/d3d9.dll" > x32/d3d9.dll
sudo cat "$CHROOT/root/build/b64/src/d3d9/d3d9.dll" > x64/d3d9.dll
ls -la x32/d3d9.dll x64/d3d9.dll
