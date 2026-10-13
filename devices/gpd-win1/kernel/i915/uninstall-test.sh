#!/bin/sh
# Run ON THE GPD: remove the one-shot i915 test entry and its initramfs (see install-test.sh).
set -e
sudo rm -f /boot/loader/entries/arch-test.conf /boot/initramfs-linux-test.img
sudo rm -rf /var/lib/gpd-test-modroot
sudo bootctl list --no-pager 2>/dev/null | grep -E "title|id:" || true
