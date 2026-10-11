#!/bin/sh
# Run ON THE GPD (sudo): install the patched mesa package built on the desktop (mesa/build.sh), after saving
# Arch's installed mesa package to /var/cache/gpd-mesa-stock for rollback.sh (paccache may clean the pacman
# cache). Only the `mesa` package is replaced; the patch is in crocus (OpenGL), vulkan-intel stays Arch's.
#   mesa/install.sh /path/to/mesa-1:26.2.4-1.2-x86_64.pkg.tar.zst
# Takes effect for programs started afterwards (sway keeps the old Mesa until it restarts / reboot).
set -e
pkg=$1
[ -f "$pkg" ] || { echo "usage: $0 mesa-<ver>.pkg.tar.zst"; exit 1; }
cur=$(pacman -Q mesa | cut -d' ' -f2)
case "$cur" in
*-*.*) echo "installed mesa $cur is already a GPD build; stock package kept from before:";;
*)  sudo mkdir -p /var/cache/gpd-mesa-stock
    f=/var/cache/pacman/pkg/mesa-$cur-x86_64.pkg.tar.zst
    [ -f "$f" ] || { echo "Arch's $f not in the pacman cache: download it first (rollback needs it)"; exit 1; }
    sudo cp -n "$f" "$f.sig" /var/cache/gpd-mesa-stock/ 2>/dev/null || sudo cp -n "$f" /var/cache/gpd-mesa-stock/;;
esac
ls -la /var/cache/gpd-mesa-stock/
sudo pacman -U --noconfirm "$pkg"
pacman -Q mesa
