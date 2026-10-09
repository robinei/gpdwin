#!/bin/sh
# Remove the patched audio modules for the running kernel (the stock ones are used again at the
# next boot). Needs sudo. See kernel/install.sh.
set -e
K=$(uname -r)
D=/usr/lib/modules/$K/updates
sudo rm -f "$D/snd-soc-rt5645.ko" "$D/snd-intel-sst-core.ko"
sudo rmdir --ignore-fail-on-non-empty "$D"
sudo depmod "$K"
for m in snd_soc_rt5645 snd_intel_sst_core; do
    echo "$m -> $(modinfo -n $m)"
done
