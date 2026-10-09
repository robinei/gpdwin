#!/bin/sh
# Build and install the patched audio modules (kernel/rt5645, kernel/intel-sst) for the RUNNING kernel
# into /usr/lib/modules/<version>/updates/, which depmod prefers over the stock modules. Needs sudo.
# Reboot afterwards: never unload/reload the sound modules on a running system (it hangs the kernel,
# see docs/hardware.md "Audio"). Rerun after every kernel update (a new kernel starts with the stock
# modules; sound still works, only the hibernate bug is back). Revert: kernel/uninstall.sh.
set -e
cd "$(dirname "$0")"
K=$(uname -r)
rt5645/build.sh
intel-sst/build.sh
D=/usr/lib/modules/$K/updates
sudo install -d "$D"
sudo install -m 644 rt5645/build/snd-soc-rt5645.ko "$D/"
sudo install -m 644 intel-sst/build/sound/soc/intel/atom/sst/snd-intel-sst-core.ko "$D/"
sudo depmod "$K"
for m in snd_soc_rt5645 snd_intel_sst_core; do
    echo "$m -> $(modinfo -n $m)"
done
echo "installed for $K. Reboot to use them."
