# Licence of this folder

The kernel patches here (`i915/`, `rt5645/`, `intel-sst/`, `usbpd/`, the DKMS files and what `prepare.sh` assembles from
kernel.org sources) modify the Linux kernel and are licensed like it: **GPL-2.0-only**
(`LICENSES/GPL-2.0-only.txt` at the repo root). Copyright of the original code stays with its authors.

The investigation notes, scripts and test programs in `dsi-investigation/` and the shell scripts here are ours,
under the repo's MIT licence (`LICENSE`), except where a file says otherwise. `dsi-investigation/data/vbt.bin`
(the GPD Win's video BIOS table, extracted from its firmware) is not included; dump your own with
`sudo cat /sys/kernel/debug/dri/0/i915_vbt > vbt.bin`.
