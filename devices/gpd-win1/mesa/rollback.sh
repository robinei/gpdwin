#!/bin/sh
# Run ON THE GPD (sudo): put Arch's Mesa packages back (saved by install.sh in /var/cache/gpd-mesa-stock);
# they replace our *-gpd packages (conflict answered with --ask=4).
set -e
set -- /var/cache/gpd-mesa-stock/*-x86_64.pkg.tar.zst
[ -f "$1" ] || { echo "no saved stock packages"; exit 1; }
sudo pacman -U --noconfirm --ask=4 "$@"
pacman -Q mesa lib32-mesa vulkan-intel
