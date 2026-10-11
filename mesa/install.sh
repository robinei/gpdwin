#!/bin/sh
# Run ON THE GPD (sudo): install patched Mesa packages built on the desktop (mesa/build.sh, mesa/build.sh lib32),
# after saving Arch's installed version of each to /var/cache/gpd-mesa-stock for rollback.sh (paccache may clean
# the pacman cache). Only the packages given are replaced (mesa, lib32-mesa; the patch is in crocus = OpenGL).
#   mesa/install.sh /path/to/mesa-1:26.2.4-1.2-x86_64.pkg.tar.zst [/path/to/lib32-mesa-...pkg.tar.zst]
# Takes effect for programs started afterwards (sway keeps the old Mesa until it restarts / reboot).
set -e
[ $# -gt 0 ] || { echo "usage: $0 PKG.pkg.tar.zst..."; exit 1; }
sudo mkdir -p /var/cache/gpd-mesa-stock
for pkg in "$@"; do
    [ -f "$pkg" ] || { echo "no such file: $pkg"; exit 1; }
    name=$(pacman -Qqp "$pkg")
    cur=$(pacman -Q "$name" | cut -d' ' -f2)
    case "$cur" in
    *-*.*) echo "installed $name $cur is already a GPD build; stock package kept from before";;
    *)  f=/var/cache/pacman/pkg/$name-$cur-x86_64.pkg.tar.zst
        [ -f "$f" ] || { echo "Arch's $f not in the pacman cache: download it first (rollback needs it)"; exit 1; }
        sudo cp -n "$f" /var/cache/gpd-mesa-stock/
        [ ! -f "$f.sig" ] || sudo cp -n "$f.sig" /var/cache/gpd-mesa-stock/;;
    esac
done
ls -la /var/cache/gpd-mesa-stock/
sudo pacman -U --noconfirm "$@"
for pkg in "$@"; do pacman -Q "$(pacman -Qqp "$pkg")"; done
