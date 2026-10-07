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
- 3 min: screen off, on battery only (`idle-screen-off.sh`); on AC the screen stays dimmed.
- 10 min: suspend-then-hibernate, on battery only (`idle-suspend.sh` checks
  `bq24190-charger/online`).
- The charger state is checked when the timeout fires: unplugging while already dimmed doesn't
  turn the screen off until the next idle period.
- Games and RetroArch inhibit idle. **Gamepad input does not count as activity** for sway.

## Battery
- `low-battery.timer` every 2 min runs `/usr/local/bin/low-battery-hibernate`: hibernate at ≤5%
  while discharging.
- Bar: battery text yellow at ≤30%, red at ≤15%, `+` while charging.

## Measured power (2026-10-07, on battery, Pegasus idle, brightness 5%)
| State | Current | Power |
|---|---|---|
| Screen on | 594 mA | 2.56 W |
| Backlight 0, display powered | 448 mA | 1.92 W |
| Display powered off (`output power off`, what swayidle does) | 336 mA | 1.45 W |

Battery ~27 Wh (7.1 Ah) → ~10 h screen-on idle at 5% brightness, ~18 h display off. DPMS off
is the right "screen off" despite causing ~64 i915 irq/s while off (backlight-0 draws more).
Method: average `max170xx_battery/current_now` every 2 s for 60 s after 20 s settling, swayidle
paused, run via `swaymsg exec` (brightnessctl needs the session).

## Idle wakeups (after the Pegasus patches)
- Screen on, Pegasus idle: ~300 irq/s. Biggest source: the gamepad (USB dev `045e:028e`)
  answers its 4 ms interrupt poll with identical idle reports, 250 irq/s, whenever the screen is
  on and something has it open (Pegasus/SDL, inputd). It stops when the display is off
  (the GPD apparently powers the pad down). No driver knob for the interval; accepted.
- Pegasus: ~20 wakeups/s, 0.8% CPU (was 72/s, 2.4%) with adaptive gamepad polling (patch 0003).
- Measure per-process wakeups via `voluntary_ctxt_switches` deltas in /proc/PID/task/*/status;
  per-USB-device traffic with `usbmon` (`sudo modprobe usbmon`, read
  `/sys/kernel/debug/usb/usbmon/1u`).

## Kernel
- `kernel.nmi_watchdog = 0` (`/etc/sysctl.d/90-gpd.conf`): fewer timer wakeups.
- C-states: `intel_idle` uses its own Cherry Trail table (C1..C7S) and ignores the BIOS C-state
  limit (BIOS was at C1; Linux still spends most idle time in C7/C7S). The BIOS setting may matter
  for s2idle (S0ix), which we can't observe without debugfs: compare suspend drain if needed.

## Audio scheduling
- `realtime-privileges` (group `realtime`): PipeWire's data loops run SCHED_FIFO (rtprio 83-88)
  without RTKit; the RTKit warnings at startup are gone (verified after reboot 2026-10-07).

## Logs
- journald capped at 200 MB (`/etc/systemd/journald.conf.d/90-gpd.conf`).

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
- Verified 2026-10-07 (actual clocks from /proc/cpuinfo, APERF/MPERF based):
  - CPU idle: unused cores at 480 MHz (hardware minimum), cores mostly in C7/C7S.
  - CPU turbo on: 1 busy core 2.4 GHz; all 4 cores 2.4 GHz (46 C after 9 s, fan off).
  - `turbo.sh cpu` (Mod4+F11): caps all cores at 1.6 GHz under load, toggling again restores
    2.4 GHz. Works from scripts too (wheel can write `no_turbo`).
  - GPU idle: act 0 MHz with ~100% RC6 residency (deep idle); limits RPn 200 / RP1 400 / RP0 600.
  - GPU under load (RetroArch + crt-royale): turbo on reached 420-540 MHz; with `turbo.sh gpu`
    (Mod4+F12, limit 400) it never exceeded the limit. RPS ramps conservatively when the load is
    partly CPU-bound; fully saturating 600 MHz wasn't shown (no GPU-only benchmark installed).
- Test hygiene: RetroArch ignores SIGTERM; use `--max-frames` (exits by itself, uncapped) plus
  `timeout -s KILL`, and power the output on first (no frames are drawn while it is off). The
  GPD Win has a physical fan switch; heavy loads with the fan off cause audible coil whine.

## Memory / swap
- zswap is on by default in the Arch kernel (zstd, 20% pool). zram was tried and removed; don't
  re-suggest it. `vm.swappiness`/`page-cluster` at defaults.
- RAM at idle ~760 MB used, of which Claude Code ~400 MB when running.

## CPU vulnerability mitigations: off
`mitigations=off` on the kernel command line (decided 2026-10-07: gaming-only device, little web
browsing; only Firefox running untrusted JavaScript would be a realistic attack path). Affected on
this Airmont CPU: Meltdown (PTI, without PCID so every kernel entry flushes the TLB), MDS (VERW buffer
clearing), Spectre v1/v2. Measured on the same device, default vs off:

| Test | default | off |
|---|---|---|
| getppid syscall | 1946 ns | 192 ns |
| 64-byte read from /dev/zero | 2070 ns | 268 ns |
| mmap + 16 page faults + munmap | 97 us | 62 us |
| pipe ping-pong between processes | 31 us | 22 us |
| RetroArch snes9x, 1800 frames at fixed 1.6 GHz (user+kernel CPU s) | 13.55 | 13.40 (~1%) |
| Hyper Light Drifter title screen at fixed 1.6 GHz, system CPU busy | 55.0% | 48.7% (~12% less) |
| 5x `wine cmd /c exit` wall time | 1.3-1.7 s | 1.2-1.4 s |

Emulators barely notice; Wine games (wineserver round trips, futexes, GPU ioctls) gain clearly.
To revert: remove `mitigations=off` from `system/boot/loader/entries/arch.conf`, `scripts/sync apply`.

## Memory
- `vm.swappiness = 10` (sysctl.d/90-gpd.conf): swap is the eMMC partition used for hibernation;
  keep game memory in RAM as long as possible.
- Transparent huge pages left at the Arch default `enabled=always, defrag=madvise`: Wine, glibc
  malloc and emulators don't madvise, so `madvise` would cost them huge pages (more TLB misses on
  the small Airmont TLB) for only ~5 khugepaged wakeups/s saved. Allocation never stalls on
  compaction with defrag=madvise.
