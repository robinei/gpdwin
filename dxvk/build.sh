#!/bin/sh
# Build the GPD's 32-bit DXVK d3d9.dll on the DESKTOP (README.md): DXVK v2.4.1 + dxvk-gplasync-2.4-1.patch +
# 0001-gpd-d3d9-async-readback-and-wait-log.patch, in a clean Arch chroot with mingw-w64 (devtools, sudo).
# Output: ./d3d9.dll (x32). Copy it to the game's folder next to its exe (WINEDLLOVERRIDES d3d9=n).
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
    meson setup b32 --cross-file build-win32.txt --buildtype release -Db_ndebug=true >/build/setup.log &&
    ninja -C b32 src/d3d9/d3d9.dll >/build/ninja.log && i686-w64-mingw32-strip b32/src/d3d9/d3d9.dll'
sudo cat "$CHROOT/root/build/b32/src/d3d9/d3d9.dll" > d3d9.dll
ls -la d3d9.dll
