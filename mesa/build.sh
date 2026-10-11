#!/bin/sh
# Build the GPD's patched Mesa on the DESKTOP (Arch's PKGBUILD + our patches, see README.md) in a clean Arch
# chroot with devtools, so the packages use Arch's makepkg.conf (generic x86-64), not the desktop's CachyOS
# flags (-march=native), and link against Arch's libraries. Needs sudo (chroot). Output: ./*.pkg.tar.zst.
# The source tarball is checked by the sha256/b2 sums in PKGBUILD (same as Arch's); the PGP check is skipped
# so the build does not depend on the desktop user's keyring.
set -e
cd "$(dirname "$0")"
extra-x86_64-build -- -- --skippgpcheck
ls -la ./*.pkg.tar.zst
