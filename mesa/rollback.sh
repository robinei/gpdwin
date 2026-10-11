#!/bin/sh
# Run ON THE GPD (sudo): put Arch's Mesa packages back (saved by install.sh in /var/cache/gpd-mesa-stock).
set -e
set -- /var/cache/gpd-mesa-stock/*-x86_64.pkg.tar.zst
[ -f "$1" ] || { echo "no saved stock packages"; exit 1; }
sudo pacman -U --noconfirm "$@"
pacman -Q mesa lib32-mesa
