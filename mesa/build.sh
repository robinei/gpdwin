#!/bin/sh
# Build the GPD's patched Mesa on the DESKTOP (Arch's PKGBUILD + our patches, see README.md) in a clean Arch
# chroot (devtools mkarchroot/makechrootpkg), so the packages get Arch's generic x86-64 flags
# (devtools' makepkg.conf, not the desktop's CachyOS -march=native) and Arch's libraries. The chroot uses
# chroot-pacman.conf (official Arch mirrors, own cache), because the desktop is CachyOS. Needs sudo.
# The source tarball is checked by the sums in PKGBUILD (same as Arch's); the PGP check is skipped so the
# build does not depend on the desktop user's keyring. Output: ./*.pkg.tar.zst
set -e
cd "$(dirname "$0")"
CHROOT=/var/lib/archbuild/gpd-x86_64
sudo mkdir -p /var/cache/gpd-archbuild/pkg
if [ ! -d "$CHROOT/root" ]; then
    sudo mkdir -p "$CHROOT"
    sudo mkarchroot -C chroot-pacman.conf -M /usr/share/devtools/makepkg.conf.d/x86_64.conf \
        -c /var/cache/gpd-archbuild/pkg "$CHROOT/root" base-devel
fi
sudo arch-nspawn -C chroot-pacman.conf -M /usr/share/devtools/makepkg.conf.d/x86_64.conf \
    "$CHROOT/root" pacman -Syuu --noconfirm
sudo makechrootpkg -c -r "$CHROOT" -- --skippgpcheck
ls -la ./*.pkg.tar.zst
