# Power, sleep, battery

Only s2idle is available (`/sys/power/mem_sleep` = `[s2idle]`, no S3).

## Sleep
- `/etc/systemd/sleep.conf.d/handheld.conf`: `MemorySleepMode=s2idle`, `HibernateDelaySec=30min`.
- Power button (inputd, desktop.md): with the screen on, a press puts the device to sleep
  (suspend-then-hibernate). With the screen dark it never sleeps: hold ~1 s and it comes on, whatever
  the state: hibernated/off (the hardware needs the hold), asleep, or screen off on idle (inputd
  only turns the screen on). Presses within 3 s of a resume are ignored, so the press that woke it
  doesn't put it back to sleep (`/usr/lib/systemd/system-sleep/resume-time` writes `/run/resume-time`).
- `/etc/systemd/logind.conf.d/handheld.conf`: power key ignored (`HandlePowerKey=ignore`, inputd handles it); lid switch is ignored (`HandleLidSwitch=ignore`); `inputd` disables input, turns the screen off (`screen.sh`, games frozen) and throttles CPU/GPU (`lid.sh`) while it is closed (desktop.md).
- Hibernation: 4 GB swap partition (UUID in fstab). Resume works without `resume=`: the systemd
  initramfs finds the image via the `HibernateLocation` EFI variable. The RTC alarm does not wake
  the device from hibernation; the power button does. Resume takes ~15-30 s.
- Suspend needed the xHCI wakeup fix (hardware.md).
- Lid closed = awake but frozen (~1.5 W) until the 30 min idle suspend (or 30 min after unplugging,
  if closed on AC), then suspend-then-hibernate
  (hibernate after another 30 min, ~0 W). Deliberate tradeoff (Robin, 2026-10-10): opening the lid
  within 30 min resumes instantly; a closed lid costs ~0.75 Wh (~3%) before it sleeps. Overnight
  2026-10-09/10: 84% → ~81% including that and ~30 min of morning use.
- The lid does not wake the device; the power button does. The lid's wake source is an ACPI GPIO
  event (`INT33FF:00` pin 35, IRQ "ACPI:Event"); it woke the device on close *and* open, and a
  lid-close wake left it awake with the lid shut (logind ignores the lid). Disabled with kernel
  `gpiolib_acpi.ignore_wake=INT33FF:00@35`; lid events while awake still work. Check:
  `/sys/kernel/irq/<irq>/wakeup` = disabled. Which IRQ woke the device: `/sys/power/pm_wakeup_irq`.

## Idle (inputd, `src/inputd.c`; swayidle until 2026-10-11)
- The times are the "Idle policy" `#define`s at the top of `src/inputd.c` (battery and AC value per
  step, 0 = never; dim level `DIM_PERCENT`). After a change: `scripts/sync install`, `pkill -x inputd`.
- 2 min: dim to a third of current brightness, min 1% (the brightness is saved; restored on
  activity).
- 3 min: on battery, dim to 1%. On AC: 10 min.
- 5 min: screen off (`screen.sh off`), on battery only; on AC the screen stays on (dimmed).
- 30 min: suspend-then-hibernate, on battery only.
- Activity: keyboard, pointer and touch (sway tells inputd over ext-idle-notify after 2 min
  without them, and not while an idle inhibitor is active: games and RetroArch inhibit idle),
  gamepad use (sway doesn't count gamepads; inputd reads the pad), opening the lid, the power
  button on a dark screen, plugging the charger in or out, waking from sleep. Activity undoes the
  steps.
- The steps are a function of idle time, charger and lid at every moment, not fired once: so
  plugging in with the screen off turns it on (dimmed: AC rules), and unplugging after a long idle
  on AC starts the battery countdown from the unplug. (With swayidle, steps fired once per idle
  period and checked the charger only then: a lid closed on AC for 30+ min and then unplugged
  never slept.)
- Lid closed: idle runs from the close (or a later unplug or wake), ignores idle inhibitors (the
  game is frozen), and only the suspend step applies; the lid has the screen.

## Battery
- `low-battery.timer` every 2 min runs `/usr/local/bin/low-battery-hibernate`: hibernate at ≤5%
  while discharging.
- Bar: `bat 80%` on battery (yellow at ≤30%, red at ≤15%), `bat 80%+` charging, `bat full`
  charged with the charger connected, `bat 85% max` when held at the charge limit (battery
  within 100 mV of it; see "Charge limit"), `bat 80% not charging` (yellow) when connected but the
  charger doesn't charge (see below). The cable comes from `bq24190-charger/online`, the state
  from its `status` (which says "Full", not "Charging", once topped up).

## Charge limit (2026-10-12)
- The pack is a high-voltage Li-ion (MAX17047 `voltage_max` 4.4 V); the bq24292i charged it to
  4.384 V (`constant_charge_voltage`, max 4400000). 279 cycles then, capacity still at/above design.
- `/usr/local/bin/charge-limit` sets 4.144 V (~80% on such cells) and makes the file wheel-writable;
  run by `charge-limit.service` at boot (enabled by hand, waits up to 60 s for the charger) and by
  `90-charge-limit.rules` when the driver (re)registers; both log to `journalctl -t charge-limit` /
  `-u charge-limit`. The rule alone failed at the 2026-10-12 00:29 boot: udev never processed the
  power_supply add events (no USEC_INITIALIZED on any of them), so it stayed at 4.384 V. The driver
  keeps the value and rewrites it on resume. Voltage is what stresses the cells
  (with heat), so this limits wear and swelling directly; no daemon, the chip stops by itself.
- Charge-path: the bq24292i powers the system from the adapter; once charged the battery rests
  (measured 1.5 mA `current_now` at full on the 9 V PD charger), it is not cycled while plugged in.
- Above the limit (just after lowering it) the battery runs the system until it is down to the limit
  (measured -0.6 A at 4.30 V), and the charger says "Not charging": the bar shows `bat N% max` then.
  Once at the limit the charger tops up and says "Full" (`bat full`). 
- Full charge for a trip: shelf Utilities > Full Charge (`pegasus/utils/tools/charge.sh`) toggles
  4.144 V ↔ 4.384 V, no password (charge-limit makes the file writable by wheel), shows it in wob
  (yellow bar at 80 = limited, full = 100%). Back to the limit at the next boot.

## Charging stuck at 500 mA after hibernate (fixed 2026-10-10)
- Seen once: after a hibernate wake, plugging in the PD charger (9 V / 3 A) left
  `bq24190-charger/input_current_limit` at 500000, status "Not charging", battery slowly
  discharging. Replugging didn't help; a reboot did. Kernel log at plug-in: `Can't read SS reg: -110`
  (I2C timeouts through the PMIC's charger I2C adapter, `i2c-cht-wc`).
- How the limit is set: the bq24190's own detection (D+/D- not wired, `f_vbus_stat=1`) gives 500 mA on
  every VBUS rise; 300 ms after the USB-C side (tcpm) reports a change, `bq24190_input_current_limit_work`
  copies tcpm's `current_max` into the chip. That write is one-shot: errors are ignored, no retry.
- Cause (confirmed by the sleep hook's log, 2026-10-10 15:03): firmware resets the Whiskey Cove PMIC's
  charger registers on hibernate wake. `CHGRCTRL0` 0x38 → 0x10 (SWCONTROL and CCSM_OFF cleared: the
  PMIC's own charger state machine is back and fights the kernel over the bq24190), `CHGDISCTRL`
  0x10 → 0x50 (charge-disable pin under hardware control), IRQ masks `PWRSRC_IRQ_MASK` 0x86 → 0x9f,
  `EXTCHGRIRQ_MSK` 0x10 → 0x1f, `IRQLVL1_MASK` 0xae → 0xbf (plug-in and charger-I2C interrupts
  masked). Linux writes these only at probe (`extcon-intel-cht-wc`, `i2c-cht-wc`, the MFD), so a
  reboot fixes it.
- Fix: `/usr/lib/systemd/system-sleep/pmic-charger` saves these registers before sleep and writes
  back any that differ after wake (`i2cset` on i2c-5). Restoring them alone was not enough (tested 2026-10-10 15:23): the
  firmware also resets the FUSB302 USB-C controller (i2c-0 0x22, `CONTROL2` toggle bit off), so
  plugging in raised no interrupt (`fsc_interrupt_int_n` count 0), tcpm stayed offline and the
  bq24190 stayed at its own 500 mA (the bar still showed `+`: the chip charges, but the battery
  net-discharged ~630 mA). The fusb302 driver has no hibernate-restore callback; after a hibernate
  wake the hook rebinds it (`/sys/bus/i2c/drivers/typec_fusb302/{unbind,bind}` `i2c-fusb302`), which
  re-initialises the chip; tested: 9 V / 3 A negotiated, +1.04 A into the battery. It logs the charger state before/after and
  what it restored: `journalctl -t pmic-charger`.
- Workaround if it recurs: `echo 3000000 | sudo tee /sys/class/power_supply/bq24190-charger/input_current_limit`,
  or reboot.

## Measured power (2026-10-07, on battery, Pegasus idle, brightness 5%)
| State | Current | Power |
|---|---|---|
| Screen on | 594 mA | 2.56 W |
| Backlight 0, display powered | 448 mA | 1.92 W |
| Display powered off (`output power off`, what the idle screen-off does) | 336 mA | 1.45 W |

Battery ~27 Wh (7.1 Ah) → ~10 h screen-on idle at 5% brightness, ~18 h display off. DPMS off
is the right "screen off" despite causing ~64 i915 irq/s while off (backlight-0 draws more).
Method: average `max170xx_battery/current_now` every 2 s for 60 s after 20 s settling, swayidle
paused, run via `swaymsg exec` (brightnessctl needs the session).

## Idle wakeups (2026-10-07, with Pegasus as the frontend then)
- Screen on, Pegasus idle: ~300 irq/s. Biggest source: the gamepad (USB dev `045e:028e`)
  answers its 4 ms interrupt poll with identical idle reports, 250 irq/s, whenever the screen is
  on and something has it open (Pegasus/SDL, inputd). It stops when the display is off
  (the GPD apparently powers the pad down). No driver knob for the interval; accepted.
- Pegasus: ~20 wakeups/s, 0.8% CPU (was 72/s, 2.4%) with adaptive gamepad polling (patch 0003).
  shelf, which replaced it, sleeps in poll() while untouched (0 wakeups/s of its own).
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
- `Mod4+t` toggles CPU turbo (`/sys/devices/system/cpu/intel_pstate/no_turbo`), `Mod4+g`
  toggles GPU turbo (i915 max+boost 600 MHz ↔ RP1 400 MHz). Script `dotfiles/sway/turbo.sh
  cpu|gpu` reads the state back and shows it in wob (full = on; purple CPU, green GPU, styles in
  `dotfiles/wob/wob.ini`).
- `/etc/tmpfiles.d/turbo.conf` makes `no_turbo`, `rps_max_freq_mhz`, `rps_boost_freq_mhz`
  group-writable by `wheel` at boot. Both reset to turbo-on at boot.
- cpufreq driver `intel_cpufreq`, governor `schedutil`.
- Verified 2026-10-07 (actual clocks from /proc/cpuinfo, APERF/MPERF based):
  - CPU idle: unused cores at 480 MHz (hardware minimum), cores mostly in C7/C7S.
  - CPU turbo on: 1 busy core 2.4 GHz; all 4 cores 2.4 GHz (46 C after 9 s, fan off).
  - `turbo.sh cpu` (Mod4+t): caps all cores at 1.6 GHz under load, toggling again restores
    2.4 GHz. Works from scripts too (wheel can write `no_turbo`).
  - GPU idle: act 0 MHz with ~100% RC6 residency (deep idle); limits RPn 200 / RP1 400 / RP0 600.
  - GPU under load (RetroArch + crt-royale): turbo on reached 420-540 MHz; with `turbo.sh gpu`
    (Mod4+g, limit 400) it never exceeded the limit. RPS ramps conservatively when the load is
    partly CPU-bound; fully saturating 600 MHz wasn't shown (no GPU-only benchmark installed).
- Test hygiene: RetroArch ignores SIGTERM; use `--max-frames` (exits by itself, uncapped) plus
  `timeout -s KILL`, and power the output on first (no frames are drawn while it is off). The
  GPD Win has a physical fan switch; heavy loads with the fan off cause audible coil whine.

## Memory / swap
- zswap is on by default in the Arch kernel (zstd, 20% pool). zram was tried and removed; don't
  re-suggest it. `vm.swappiness`/`page-cluster` at defaults.
- RAM at idle: ~505 MB "used" of 3.8 GB right after boot with shelf (`free` = total - available, so the freeable file cache (~230 MB) is not in it; actually held ~280-300 MB: programs 77, kernel ~100, shmem 27, unevictable 70, the rest kernel reserves) (2026-10-11; sway 72 MB RSS incl. the GPU driver, shelf 29, wireplumber 23, swaybar 15); Claude Code adds ~400 MB when running. Was ~760 MB with Claude Code in the Pegasus days.

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

## Runtime PM
- TXE (`00:1a.0`, mei_txe) runtime-suspends via `90-txe-runtime-pm.rules`; the kernel left it
  `control=on`. A/B with dimmed screen on battery: 2131 vs 2047 mW (~80 mW, noisy).
- Still `control=on`, deliberately: Broadcom WiFi (brcmfmac has no runtime PM), the keyboard and
  pad USB devices (the pad's 250 Hz polling keeps the xHCI awake anyway while the screen is on).
- Idle power 2026-10-07: dimmed screen on ~2.05 W, screen off ~1 W (WiFi connected; the
  SoC only reaches S0i3 in real suspend).
