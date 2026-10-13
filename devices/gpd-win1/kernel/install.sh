#!/bin/sh
# Install (or update) our DKMS module packages: gpd-audio (sound after hibernation) and gpd-usbpd (5 V-only
# USB-PD charging). The same as `scripts/sync install` does for the manifest's "dkms" entries; DKMS rebuilds
# them for every new kernel (pacman hook of the dkms package). Needs `sudo -v` first.
# Reboot afterwards: never unload/reload the sound modules on a running system (it hangs the
# kernel, see devices/gpd-win1/docs/hardware.md "Audio"). Revert: kernel/uninstall.sh [package].
set -e
cd "$(dirname "$0")"
exec ../../../scripts/sync install /usr/src/gpd-
