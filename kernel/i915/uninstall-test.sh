#!/bin/sh
# Run ON THE GPD: remove the i915 test boot entry and its initramfs (see install-test.sh).
# Normal boot never used them; this only cleans up. If the test entry was made the default,
# this switches the default back to arch.conf.
set -e
sudo bootctl set-default arch.conf
sudo rm -f /boot/loader/entries/arch-i915test.conf /boot/initramfs-linux-i915test.img
sudo rm -f /usr/lib/modules/7.2.9-arch1-1/updates/i915.ko.zst
sudo depmod 7.2.9-arch1-1
bootctl list 2>/dev/null | grep -E "title|id:" || true
