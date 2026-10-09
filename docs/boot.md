# Boot

## Layout
- GPT on the eMMC: p1 512 MiB ESP mounted at `/boot` (FAT32, kernels live here), p2 4 GiB swap,
  p3 ext4 `/`. fstab and the boot entry use UUIDs.
- systemd-boot (`systemd-boot-update.service` enabled). `loader.conf`: `timeout 0`,
  `console-mode keep`, `editor no`. Menu hidden (timeout 0): **hold Space while powering on** to get it. From
  the running system: `systemctl reboot --boot-loader-entry=arch-stock.conf` (one boot with the stock entry;
  Pegasus Utilities "Reboot (stock display driver)", sudoers `22-boot-stock`) or
  `systemctl reboot --boot-loader-menu=10` (menu for 10 s once). Default `arch.conf`. Second entry
  `arch-stock.conf` = `initramfs-linux-stock.img` with Arch's own modules (stock i915) as fallback for the
  patched i915 (docs/hardware.md "Display"); built by `/usr/local/bin/gpd-stock-initramfs` (pacman hook
  `95-gpd-stock-initramfs.hook`). Entry `arch.conf`: `linux` kernel, options
  `root=UUID=... rw fbcon=rotate:1 mitigations=off quiet loglevel=3 rd.udev.log_level=3
  vt.global_cursor_default=0` (silent boot: no kernel/udev text, no console cursor; systemd
  status lines only on failures). `arch-stock.conf` stays verbose for troubleshooting.
  Microcode is embedded in the initramfs (`microcode` hook), so `/boot/intel-ucode.img` is
  unused by the entry.
- mkinitcpio: systemd-based hooks (Arch default), `MODULES=(pwm-lpss-platform i915)`, no fallback
  image.
- Autologin: `getty@tty1` drop-in `autologin.conf` (`-o '-p -f -- \\u' --skip-login --noissue --nohostname
  --nonewline --autologin robin`: no banner or login line, and the screen is cleared); `~/.hushlogin` hides
  "Last login"; fish `conf.d/sway.fish` runs `exec sway` on tty1. Sway gets its seat from seatd.
- Shutdown: no hardware watchdog (`system.conf.d/90-gpd.conf` `RebootWatchdogSec=0`), so no
  "watchdog did not stop!" lines; a hung shutdown is no longer force-rebooted after 10 min.

## Timings (`systemd-analyze`)
| Boot | Firmware | Loader | Kernel | Initrd | Userspace | Total | getty@tty1 at |
|---|---|---|---|---|---|---|---|
| First boot after install | 9.7s | 2.0s | 2.6s | 4.9s | 8.1s | 27.4s | |
| Baseline (before tweaks) | 7.1s | 2.0s | 2.6s | 5.0s | 7.7s | 24.4s | 7.68s |
| After user-sessions override | 7.9s | 1.9s | 2.5s | 3.5s | 6.1s | 21.9s | 4.94s |

The override saves ~2.7s on the login path (iwd no longer in getty's critical chain). The initrd
drop in that row is probably noise; firmware varies ~0.8s between boots. Sway now starts ~13s
after kernel start (was 16s).

## Logins don't wait for the network
- Full override `/etc/systemd/system/systemd-user-sessions.service`: the stock unit without
  `network.target` in `After=`. Must be a full override: an empty `After=` in a drop-in does not
  remove ordering (tested). It is a frozen copy: recheck after systemd updates.
- Verify: `systemd-analyze critical-chain getty@tty1.service` should not list `iwd`.
- Revert: `sudo rm /etc/systemd/system/systemd-user-sessions.service && sudo systemctl daemon-reload`
  (and remove it from manifest.tsv).

## Where the time goes
- initrd is CPU-bound module loading (udev coldplug ~3s CPU on the Atom), then
  `initrd-switch-root` 2.3s cold-reading the real root from eMMC; loader 2s reading the 17 MB
  kernel + 21 MB initrd. Firmware is ~7s and only changeable in the BIOS. Ideas: see ideas.md.
