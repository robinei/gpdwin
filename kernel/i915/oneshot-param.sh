#!/bin/sh
# Run ON THE GPD (sudo): boot ONCE with extra kernel parameters, e.g.
#   kernel/i915/oneshot-param.sh i915.vlv_dsi_burst_pct=120
# Creates /boot/loader/entries/arch-param-test.conf (= arch.conf + the parameters, same initramfs)
# and makes it the one-shot entry for the next boot. Every later boot is the normal default again.
# Remove the entry: sudo rm /boot/loader/entries/arch-param-test.conf
set -e
[ $# -ge 1 ] || { echo "usage: $0 param=value ..."; exit 1; }
sudo sh -c "sed 's|^title .*|title Arch Linux (test: $*)|; s|^options \(.*\)|options \1 $*|' \
    /boot/loader/entries/arch.conf > /boot/loader/entries/arch-param-test.conf"
sudo cat /boot/loader/entries/arch-param-test.conf
sudo grep -q "^options .* $*\$" /boot/loader/entries/arch-param-test.conf
sudo bootctl set-oneshot arch-param-test.conf
echo "Next boot (only): arch-param-test.conf. Check after boot: cat /proc/cmdline"
