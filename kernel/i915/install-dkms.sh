#!/bin/sh
# Run ON THE GPD (sudo): install the patched i915 as DKMS package gpd-i915, rebuilt automatically for
# every kernel update by the dkms pacman hook (takes long on the Atom; scripts/update and /maintain
# warn before a kernel update). Patches: the PATCHES list below. The module lands in
# /usr/lib/modules/<kernel>/updates/ and the normal initramfs picks it up. If a build fails (download,
# patch not applying to a new kernel, compile error) nothing is installed and the stock i915 is used.
# Remove: kernel/i915/uninstall-dkms.sh.
set -e
cd "$(dirname "$0")"
PATCHES="0001 0011 0012 0013"
SRC=/usr/src/gpd-i915-1.0
sudo pacman -S --needed --noconfirm dkms linux-headers curl zstd
sudo dkms remove gpd-i915/1.0 --all 2>/dev/null || true
sudo rm -rf "$SRC"
sudo mkdir -p "$SRC/patches"
sudo cp dkms/dkms.conf dkms/prepare-source.sh "$SRC/"
for n in $PATCHES; do sudo cp "$n"-*.patch "$SRC/patches/"; done
ls "$SRC/patches"
sudo dkms add gpd-i915/1.0
start=$(date +%s)
sudo dkms install gpd-i915/1.0
echo "build+install took $(( $(date +%s) - start )) s"
dkms status gpd-i915
echo "i915 -> $(modinfo -n i915)  srcversion $(modinfo -F srcversion i915)"
sudo mkinitcpio -P
echo "Reboot to use it (the boot entry arch.conf now loads the patched i915)."
