#!/bin/sh
# Run ON THE GPD (sudo): put Arch's mesa package back (saved by install.sh in /var/cache/gpd-mesa-stock).
set -e
f=$(ls -t /var/cache/gpd-mesa-stock/mesa-*-x86_64.pkg.tar.zst | head -1)
[ -n "$f" ] || { echo "no saved stock mesa package"; exit 1; }
sudo pacman -U --noconfirm "$f"
pacman -Q mesa
