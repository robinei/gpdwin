#!/bin/sh
# Run ON THE GPD (sudo): install the patched i915 as DKMS package gpd-i915, rebuilt automatically for
# every kernel update by the dkms pacman hook (takes long on the Atom; scripts/update and /maintain
# warn before a kernel update). Patches: the PATCHES list below. The module lands in
# /usr/lib/modules/<kernel>/updates/ and the normal initramfs picks it up. If a build fails (download,
# patch not applying to a new kernel, compile error) nothing is installed and the stock i915 is used.
# Also sets up the completely stock fallback entry (arch-stock.conf). Remove: kernel/i915/uninstall-dkms.sh.
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
# Completely stock fallback: initramfs with Arch's modules only + boot entry "Arch Linux (stock)".
# /usr/local/bin/gpd-stock-initramfs and its pacman hook come from manifest.tsv (scripts/sync install).
sudo /usr/local/bin/gpd-stock-initramfs
sudo sh -c 'sed "s|^title .*|title Arch Linux (stock)|; s|^initrd .*initramfs-linux.img|initrd /initramfs-linux-stock.img|" \
    /boot/loader/entries/arch.conf > /boot/loader/entries/arch-stock.conf'
sudo grep -q '^initrd /initramfs-linux-stock.img' /boot/loader/entries/arch-stock.conf
# The one-shot test entry (install-test.sh) is no longer needed.
sudo rm -f /boot/loader/entries/arch-i915test.conf /boot/initramfs-linux-i915test.img
sudo bootctl set-default arch.conf
sudo bootctl set-timeout 3
bootctl list --no-pager 2>/dev/null | grep -E "title|id:" || true
echo "Reboot to use it: arch.conf loads the patched i915; 'Arch Linux (stock)' is the fallback."
