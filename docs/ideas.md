# Ideas not done yet (ranked)

1. Initrd trim: `COMPRESSION="lz4"` and/or drop `sd-vconsole` from `HOOKS` (~0.2s each,
   `mkinitcpio -P`). The initrd also carries ~51 useless i915 firmware files.
2. BIOS (Delete key or Utilities "BIOS Setup"): boot logo, Fast Boot, setup prompt timeout,
   network/PXE stack, boot order. Firmware is ~7s of the boot. Change one at a time.
3. Psychonauts native install (multilib, 32-bit libs, Xwayland). See frontend.md.
4. Gamepad activity doesn't reset sway idle (dims after 2 min while browsing the frontend with the
   pad). Options: `inhibit_idle focus` for the frontend, or have inputd inhibit while the pad
   is used.
5. Remove `clang` (build-only, zelda3-git); disable unused `systemd-userdbd.socket`.
6. Bluetooth firmware patch if Bluetooth is ever wanted.
7. GOG source for `scripts/games` (`sources/gog.py`, e.g. lgogdownloader from the AUR).
8. Gamepad navigation in `scripts/games` (it runs in foot, so keyboard only for now).

9. If DSI resume failures (`flip_done timed out`) ever appear: see hardware.md "Prior art"
    (kernel patch, `i915.disable_power_well=0`, diagnostics capture).

10. Measure s2idle battery drain (unplugged, known sleep time; method in power.md); then
    consider a shorter HibernateDelaySec or the BIOS C-state limit.

Not worth doing: disabling `sshd` (not on the boot path), zram (zswap is already on).

11. Device-only Mesa (after the upload fix is settled; `mesa/`): gallium `crocus` + `softpipe`, Vulkan
    `intel_hasvk`, no rusticl/OpenCL, video codecs, docs or other vendors' drivers. The stock `libgallium`
    (55 MB) links `libLLVM` (172 MB) into every GL process (sway, frontend, games) only for drivers this device
    never uses. Expected: faster app start, less memory, builds in minutes. Measure start time and memory
    before/after. Costs: softpipe instead of llvmpipe as fallback, a larger diff to Arch's PKGBUILD per update,
    check that nothing needs a dropped part (Xwayland, kms_swrast). Same for `lib32-mesa` (Wine).
12. Other optimizations for the one known target (Atom x7-Z8700, Silvermont/Airmont, Gen8 CHV graphics, no LLC),
    now that packages can be built for it: e.g. `-march=silvermont`/`-mtune` builds of hot packages (Mesa,
    emulator cores, SDL), Mesa/driconf defaults that suit a non-LLC GPU, trimming libraries loaded by every
    process. Find candidates by measuring first (perf, startup traces), not by guessing.
