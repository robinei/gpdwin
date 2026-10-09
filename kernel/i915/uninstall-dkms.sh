#!/bin/sh
# Run ON THE GPD: remove the DKMS package gpd-i915 (stock i915 again at the next boot).
set -e
sudo dkms remove gpd-i915/1.0 --all
sudo rm -rf /usr/src/gpd-i915-1.0 /var/cache/gpd-i915
sudo mkinitcpio -P
sudo /usr/local/bin/gpd-stock-initramfs
echo "i915 -> $(modinfo -n i915)"
