#!/bin/sh
# Run ON THE GPD, by Robin, with sudo: boot a desktop-built i915.ko.zst (kernel/i915/build.sh) ONCE,
# without touching DKMS or the normal boot entries:
#   kernel/i915/install-test.sh /path/to/i915.ko.zst [kernel parameters...]
#   e.g. kernel/i915/install-test.sh ~/i915-burst.ko.zst i915.vlv_dsi_burst_pct=120
# - /boot/initramfs-linux-test.img is built from an alternate module tree (like gpd-stock-initramfs):
#   Arch's modules (stock modules DKMS moved aside put back, no updates/) plus the test module as
#   updates/i915.ko.zst. i915 loads from the initramfs (MODULES in mkinitcpio.conf).
# - /boot/loader/entries/arch-test.conf = arch.conf with that initramfs and the extra parameters,
#   set as one-shot for the next boot. Any later boot (or a hard reset) is the normal default again.
# Remove: kernel/i915/uninstall-test.sh. The module must match the running kernel.
set -e
ko=$1; shift || true
KV=$(uname -r)
[ -f "$ko" ] || { echo "usage: $0 i915.ko.zst [kernel parameters...]"; exit 1; }
zstd -dcq "$ko" > /tmp/i915-test.ko
modinfo /tmp/i915-test.ko | grep -q "^vermagic: *$KV " || { echo "vermagic mismatch (running $KV)"; exit 1; }
echo "srcversion of the test module: $(modinfo -F srcversion /tmp/i915-test.ko)"
rm -f /tmp/i915-test.ko
R=/var/lib/gpd-test-modroot
sudo sh -s "$KV" "$R" "$(readlink -f "$ko")" <<'EOS'
set -e
KV=$1 R=$2 KO=$3
rm -rf "$R"
mkdir -p "$R/usr/lib/modules"
ln -s usr/lib "$R/lib"
cp -al "/usr/lib/modules/$KV" "$R/usr/lib/modules/"
rm -rf "$R/usr/lib/modules/$KV/updates"
for o in /var/lib/dkms/*/original_module/"$KV"/*/*.origin; do
    [ -e "$o" ] || continue
    dest=$(cat "$o"); mkdir -p "$R$(dirname "$dest")"; ln -f "${o%.origin}" "$R$dest"
done
install -Dm644 "$KO" "$R/usr/lib/modules/$KV/updates/i915.ko.zst"
depmod -b "$R" "$KV"
echo "test initramfs i915 -> $(modinfo -b "$R" -k "$KV" -F filename i915)"
mkinitcpio -k "$KV" -r "$R" -g /boot/initramfs-linux-test.img
EOS
params="$*"
sudo sh -c "sed 's|^title .*|title Arch Linux (i915 test${params:+: $params})|; s|^initrd .*initramfs-linux.img|initrd /initramfs-linux-test.img|; s|^options \(.*\)|options \1${params:+ $params}|' \
    /boot/loader/entries/arch.conf > /boot/loader/entries/arch-test.conf"
sudo cat /boot/loader/entries/arch-test.conf
sudo grep -q '^initrd /initramfs-linux-test.img' /boot/loader/entries/arch-test.conf
sudo bootctl set-oneshot arch-test.conf
echo "Ready: the next boot (only) uses the test module. Check after boot: cat /sys/module/i915/srcversion"
