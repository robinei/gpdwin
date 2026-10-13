#!/bin/sh
# Build the GPD's DXVK d3d9.dll on the DESKTOP (README.md) in a clean Arch chroot with mingw-w64 (devtools, sudo):
#   ./build.sh          v2.4.1 + dxvk-gplasync-2.4-1.patch + 0001-gpd-d3d9-async-readback-and-wait-log.patch -> ./x32, ./x64
#   ./build.sh 2.6.2    v2.6.2 + v262/dxvk-gplasync-2.6.2-1.patch + vk13/0001-...-2.7.1.patch    -> ./v262/x32, x64
#   ./build.sh 2.7.1    v2.7.1 + vk13/dxvk-gplasync-2.7.1-1.patch + vk13/0001-...-2.7.1.patch    -> ./vk13/x32, x64
# (reserves: 2.5+ costs more CPU per frame here, 2.7+ needs hasvk_report_descriptor_indexing=true; README.md)
# Output: x32/d3d9.dll and x64/d3d9.dll. The game installer (scripts/gamelib/core.py) copies the one matching
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
ver=${1:-2.4.1}
case $ver in
    2.4.1) out=. gpl=dxvk-gplasync-2.4-1.patch gpd=0001-gpd-d3d9-async-readback-and-wait-log.patch ;;
    2.6.2) out=v262 gpl=v262/dxvk-gplasync-2.6.2-1.patch gpd=vk13/0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch ;;
    2.7.1) out=vk13 gpl=vk13/dxvk-gplasync-2.7.1-1.patch gpd=vk13/0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch ;;
    *) echo "usage: $0 [2.4.1|2.6.2|2.7.1]"; exit 1 ;;
esac
sudo rm -rf "$CHROOT/root/build"
sudo git clone -q --branch "v$ver" --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/doitsujin/dxvk "$CHROOT/root/build"
sudo cp "$gpl" "$CHROOT/root/build/gpl.patch"
sudo cp "$gpd" "$CHROOT/root/build/gpd.patch"
sudo arch-nspawn -C "$CONF" "$CHROOT/root" sh -c 'cd /build && patch -Np1 -s < gpl.patch &&
    patch -Np1 -s < gpd.patch &&
    meson setup b32 --cross-file build-win32.txt --buildtype release -Db_ndebug=true >/build/setup32.log &&
    ninja -C b32 src/d3d9/d3d9.dll >/build/ninja32.log && i686-w64-mingw32-strip b32/src/d3d9/d3d9.dll &&
    meson setup b64 --cross-file build-win64.txt --buildtype release -Db_ndebug=true >/build/setup64.log &&
    ninja -C b64 src/d3d9/d3d9.dll >/build/ninja64.log && x86_64-w64-mingw32-strip b64/src/d3d9/d3d9.dll'
mkdir -p "$out/x32" "$out/x64"
sudo cat "$CHROOT/root/build/b32/src/d3d9/d3d9.dll" > "$out/x32/d3d9.dll"
sudo cat "$CHROOT/root/build/b64/src/d3d9/d3d9.dll" > "$out/x64/d3d9.dll"
ls -la "$out/x32/d3d9.dll" "$out/x64/d3d9.dll"; sha256sum "$out/x32/d3d9.dll" "$out/x64/d3d9.dll"
