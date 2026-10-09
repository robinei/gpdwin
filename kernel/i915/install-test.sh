#!/bin/sh
# Run ON THE GPD, by Robin, with sudo: install a patched i915.ko.zst (from build.sh on the desktop)
# as a separate, one-shot boot entry. Nothing in the normal boot changes:
#   - /boot/initramfs-linux-i915test.img gets the patched module (i915 loads from the initramfs,
#     MODULES in mkinitcpio.conf); /boot/initramfs-linux.img and /usr/lib/modules stay stock.
#   - /boot/loader/entries/arch-i915test.conf = arch.conf with that initramfs.
#   - bootctl set-oneshot (+ a 5 s menu, set-timeout-oneshot): only the NEXT boot uses it. Any later boot (or a hard reset if the
#     screen stays black) is the stock entry again.
# Usage: kernel/i915/install-test.sh /path/to/i915.ko.zst      then reboot when ready.
# Keep it for every boot: sudo bootctl set-default arch-i915test.conf (undo: set-default arch.conf).
# Remove: kernel/i915/uninstall-test.sh.
set -e
ko=$1
KV=7.2.9-arch1-1
[ -f "$ko" ] || { echo "usage: $0 i915.ko.zst"; exit 1; }
[ "$(uname -r)" = "$KV" ] || { echo "running $(uname -r), module is for $KV: rebuild"; exit 1; }
zstd -dcq "$ko" > /tmp/i915-test.ko
modinfo /tmp/i915-test.ko | grep -q "^vermagic: *$KV " || { echo "vermagic mismatch"; exit 1; }
echo "srcversion of the test module: $(modinfo -F srcversion /tmp/i915-test.ko)"
rm -f /tmp/i915-test.ko
upd=/usr/lib/modules/$KV/updates/i915.ko.zst
[ ! -e "$upd" ] || { echo "$upd exists already, refusing"; exit 1; }
# The patched module is in updates/ only while the test initramfs is generated.
sudo install -Dm644 "$ko" "$upd"
sudo depmod "$KV"
ok=0
sudo mkinitcpio -k "$KV" -g /boot/initramfs-linux-i915test.img && ok=1
sudo rm -f "$upd"
sudo depmod "$KV"
[ $ok = 1 ] || { echo "mkinitcpio failed"; exit 1; }
echo "stock module again on disk: $(modinfo -F srcversion i915)"
sudo sh -c 'sed "s|^title .*|title Arch Linux (i915 test)|; s|^initrd .*initramfs-linux.img|initrd /initramfs-linux-i915test.img|" \
    /boot/loader/entries/arch.conf > /boot/loader/entries/arch-i915test.conf'
sudo cat /boot/loader/entries/arch-i915test.conf
sudo grep -q '^initrd /initramfs-linux-i915test.img' /boot/loader/entries/arch-i915test.conf ||
    { echo "entry has no test initrd line; check arch.conf format"; exit 1; }
# Pin the default to the stock entry (loader.conf says "default arch.conf"; the EFI variable makes it explicit).
sudo bootctl set-default arch.conf
sudo bootctl set-oneshot arch-i915test.conf
# Show the menu for 5 s on that boot only (loader.conf has timeout 0), test entry preselected.
sudo bootctl set-timeout-oneshot 5
bootctl list 2>/dev/null | grep -A3 i915test || true
echo "Ready: the next boot (only) uses the test module. Reboot when convenient."
echo "After boot check: cat /sys/module/i915/srcversion (must be the test srcversion above)."
