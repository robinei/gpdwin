# Ideas not done yet (ranked)

1. Add `quiet` to the options in the boot entry (console is rotated with `fbcon=rotate:1`;
   printing boot messages is slow). Edit `system/boot/loader/entries/arch.conf`, `sync install`.
2. Initrd trim: `COMPRESSION="lz4"` and/or drop `sd-vconsole` from `HOOKS` (~0.2s each,
   `mkinitcpio -P`). The initrd also carries ~51 useless i915 firmware files.
3. BIOS (Delete key or Pegasus "BIOS Setup"): boot logo, Fast Boot, setup prompt timeout,
   network/PXE stack, boot order. Firmware is ~7s of the boot. Change one at a time.
4. PS1: PCSX ReARMed core (not in repos; libretro buildbot core in `~/.config/retroarch/cores/`
   or the stale AUR package). Beetle PSX needs a BIOS the user doesn't have.
5. Psychonauts native install (multilib, 32-bit libs, Xwayland). See frontend.md.
6. Gamepad activity doesn't reset sway idle (dims after 2 min while browsing Pegasus with the
   pad). Options: `inhibit_idle focus` for Pegasus, or have guide-button.py inhibit while the pad
   is used.
7. Off-device backup of this repo (private git remote or the PC). Not set up yet.
8. Remove `clang` and `yay-bin-debug`; disable unused `systemd-userdbd.socket`.
9. Bluetooth firmware patch if Bluetooth is ever wanted.

Not worth doing: disabling `sshd` (not on the boot path), zram (zswap is already on).
