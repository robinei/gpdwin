#!/bin/sh
# Remove the DKMS package with the patched audio modules (stock modules again at the next boot).
set -e
sudo dkms remove gpd-audio/1.0 --all
sudo rm -rf /usr/src/gpd-audio-1.0
for m in snd_soc_rt5645 snd_intel_sst_core; do
    echo "$m -> $(modinfo -n $m)"
done
