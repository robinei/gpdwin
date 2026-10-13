#!/bin/sh
# Install the patched audio modules (kernel/rt5645 + kernel/intel-sst) through DKMS, so they are
# rebuilt automatically for every kernel update (pacman hook of the dkms package). Needs sudo.
# Reboot afterwards: never unload/reload the sound modules on a running system (it hangs the
# kernel, see docs/hardware.md "Audio"). Revert: kernel/uninstall.sh.
set -e
cd "$(dirname "$0")"
./prepare.sh
sudo pacman -S --needed --noconfirm dkms linux-headers
sudo dkms remove gpd-audio/1.0 --all 2>/dev/null || true
sudo mkdir -p /usr/src/gpd-audio-1.0
sudo cp -a dkms-tree/gpd-audio-1.0/. /usr/src/gpd-audio-1.0/
sudo dkms add gpd-audio/1.0
sudo dkms install gpd-audio/1.0
dkms status
for m in snd_soc_rt5645 snd_intel_sst_core; do
    echo "$m -> $(modinfo -n $m)"
done
echo "Reboot to use them."
