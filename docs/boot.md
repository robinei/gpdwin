# Boot

## Layout
- GPT on the eMMC: p1 512 MiB ESP mounted at `/boot` (FAT32, kernels live here), p2 4 GiB swap,
  p3 ext4 `/`. fstab and the boot entry use UUIDs.
- systemd-boot (`systemd-boot-update.service` enabled). `loader.conf`: `timeout 0`,
  `console-mode keep`, `editor no`. Menu hidden (timeout 0): **hold Space while powering on** to get it. From
  the running system: `sudo systemctl reboot --boot-loader-entry=arch-stock.conf` (one boot with the stock entry) or
  `systemctl reboot --boot-loader-menu=10` (menu for 10 s once). Default `arch.conf`. Second entry
  `arch-stock.conf` = `initramfs-linux-stock.img` with Arch's own modules (stock i915) as fallback for the
  patched i915 (docs/hardware.md "Display"); built by `/usr/local/bin/gpd-stock-initramfs` (pacman hook
  `95-gpd-stock-initramfs.hook`). Entry `arch.conf`: `linux` kernel, options
  `root=UUID=... rw fbcon=rotate:1 mitigations=off quiet loglevel=3 rd.udev.log_level=3
  vt.global_cursor_default=0 gpiolib_acpi.ignore_wake=INT33FF:00@35 video=DP-1:d video=DP-2:d
  video=HDMI-A-1:d video=HDMI-A-2:d` (silent boot: no kernel/udev text, no console cursor; systemd
  status lines only on failures. `ignore_wake`: the lid doesn't wake from sleep, power.md.
  `video=...:d`: the external display connectors are forced off, so nothing probes them; sway spent
  ~0.25 s at startup checking the empty ports. The mini-HDMI port is not used; to use it, drop
  `video=HDMI-A-1:d` (or boot the stock entry, which keeps all ports). `arch-stock.conf` stays
  verbose for troubleshooting.
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

## Where the time goes after the kernel (2026-10-11, from the journal, process start times and one
## boot with `sway -d`; seconds after kernel start)
- getty@tty1 ~10.7, autologin session opened ~12.1: getty@.service is `Type=idle`, so systemd
  starts agetty only once all other boot jobs are done ("Startup finished" at 11.91, agetty ran
  `login` at 11.96, timestamped by a temporary `--login-program` wrapper; login itself 0.17 s).
  Fixed: `Type=simple` in the autologin drop-in. Not `pam_systemd_home` (its failed homed
  activation is logged just before, but removing it changed nothing: tried and reverted).
- User manager ~0.4 s, then fish execs sway (~12.7).
- sway until it runs its `exec` lines: ~2.3 s, no single stall: Mesa + LLVM load (cold from eMMC)
  0.63 s, EGL/GL renderer setup 0.48 s, config incl. fonts 0.3 s, libinput 0.23 s, cursor theme
  0.2 s (now wlroots' built-in cursor, nothing to load), probing the external display ports 0.25 s (now forced off on the kernel command line), modeset
  0.08 s. Then shelf ~0.07 s later.
  The GPU driver is read ahead by `gpu-prefetch.service` (enabled by hand: `systemctl enable
  gpu-prefetch.service`): at sysinit, `LD_PRELOAD=/usr/lib/libEGL_mesa.so.0 /usr/bin/true` maps and
  relocates libgallium + libLLVM, i.e. the pages sway's EGL setup touches (not the whole 220 MB),
  at low CPU/IO priority; nothing is ordered after it. Measured: driver load 0.50 s cold, 0.07 s
  after the prefetch (which takes ~0.7 s of mostly I/O, in the background).

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
