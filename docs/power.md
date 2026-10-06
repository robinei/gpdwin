# Power, sleep, battery

Only s2idle is available (`/sys/power/mem_sleep` = `[s2idle]`, no S3).

## Sleep
- `/etc/systemd/sleep.conf.d/handheld.conf`: `MemorySleepMode=s2idle`, `HibernateDelaySec=30min`.
- `/etc/systemd/logind.conf.d/handheld.conf`: lid and power key → `suspend-then-hibernate`.
- Pegasus' Suspend also does suspend-then-hibernate (dbus-send shim, see frontend.md).
- Hibernation: 4 GB swap partition (UUID in fstab). Resume works without `resume=`: the systemd
  initramfs finds the image via the `HibernateLocation` EFI variable. The RTC alarm does not wake
  the device from hibernation; the power button does. Resume takes ~15-30 s.
- Suspend needed the xHCI wakeup fix (hardware.md).

## Idle (swayidle, `dotfiles/sway/handheld`)
- 2 min: dim to a third of current brightness, min 2% (`dim.sh`; restored on activity).
- 3 min: screen off. 10 min: suspend-then-hibernate, on battery only (`idle-suspend.sh` checks
  `bq24190-charger/online`).
- Games and RetroArch inhibit idle. **Gamepad input does not count as activity** for sway.

## Battery
- `low-battery.timer` every 2 min runs `/usr/local/bin/low-battery-hibernate`: hibernate at ≤5%
  while discharging.
- Bar: battery text yellow at ≤30%, red at ≤15%, `+` while charging.

## Radios
- Bluetooth: `rfkill block bluetooth` (systemd-rfkill keeps it across boots). Undo with
  `sudo rfkill unblock bluetooth`; pairing would also need `bluez bluez-utils`.
- Wi-Fi power save is on by default (`iw dev wlan0 get power_save`).

## CPU / GPU turbo
- `Mod4+F11` toggles CPU turbo (`/sys/devices/system/cpu/intel_pstate/no_turbo`), `Mod4+F12`
  toggles GPU turbo (i915 max+boost 600 MHz ↔ RP1 400 MHz). Script `dotfiles/sway/turbo.sh
  cpu|gpu` reads the state back and shows it in wob (full = on; purple CPU, green GPU, styles in
  `dotfiles/wob/wob.ini`).
- `/etc/tmpfiles.d/turbo.conf` makes `no_turbo`, `rps_max_freq_mhz`, `rps_boost_freq_mhz`
  group-writable by `wheel` at boot. Both reset to turbo-on at boot.
- cpufreq driver `intel_cpufreq`, governor `schedutil`.

## Memory / swap
- zswap is on by default in the Arch kernel (zstd, 20% pool). zram was tried and removed; don't
  re-suggest it. `vm.swappiness`/`page-cluster` at defaults.
- RAM at idle ~760 MB used, of which Claude Code ~400 MB when running.
