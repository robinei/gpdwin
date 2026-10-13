#!/bin/sh
# Remove our DKMS module packages (stock modules again at the next boot): all, or the ones named
#   kernel/uninstall.sh [gpd-audio] [gpd-usbpd]
# scripts/sync install puts them back while they are in devices/gpd-win1/manifest.tsv.
set -e
for p in ${*:-gpd-audio gpd-usbpd}; do
    sudo dkms remove "$p/1.0" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/$p-1.0"
done
for m in snd_soc_rt5645 snd_intel_sst_core intel_chtwc_int33fe; do
    echo "$m -> $(modinfo -n $m)"
done
echo "sudo mkinitcpio -P and reboot to use the stock modules."
