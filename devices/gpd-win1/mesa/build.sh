#!/bin/sh
# Build the GPD's patched Mesa on the DESKTOP (Arch's PKGBUILD + our patches, see README.md) in a clean Arch
# chroot (devtools mkarchroot/makechrootpkg), so the packages get Arch's generic x86-64 flags
# (devtools' makepkg.conf, not the desktop's CachyOS -march=native) and Arch's libraries. The chroot uses
# chroot-pacman.conf (official Arch mirrors, own cache), because the desktop is CachyOS. Needs sudo.
# The source tarball is checked by the sums in PKGBUILD (same as Arch's); the PGP check is skipped so the
# build does not depend on the desktop user's keyring. Output: ./*.pkg.tar.zst (or lib32/*.pkg.tar.zst)
#   ./build.sh          mesa (64-bit)       ./build.sh lib32    lib32-mesa (multilib chroot)
set -e
cd "$(dirname "$0")"
here=$PWD
if [ "$1" = lib32 ]; then
    CHROOT=/var/lib/archbuild/gpd-multilib-x86_64 CONF=$here/chroot-pacman-multilib.conf PKGS="base-devel multilib-devel"
    cd lib32
else
    CHROOT=/var/lib/archbuild/gpd-x86_64 CONF=$here/chroot-pacman.conf PKGS=base-devel
fi
MAKECONF=/usr/share/devtools/makepkg.conf.d/x86_64.conf
sudo mkdir -p /var/cache/gpd-archbuild/pkg
if [ ! -d "$CHROOT/root" ]; then
    sudo mkdir -p "$CHROOT"
    sudo mkarchroot -C "$CONF" -M "$MAKECONF" -c /var/cache/gpd-archbuild/pkg "$CHROOT/root" $PKGS
fi
sudo arch-nspawn -C "$CONF" -M "$MAKECONF" "$CHROOT/root" pacman -Syuu --noconfirm
sudo makechrootpkg -c -r "$CHROOT" -- --skippgpcheck
ls -la ./*.pkg.tar.zst
