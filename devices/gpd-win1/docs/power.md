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
  4.384 V (`constant_charge_voltage`, max 4400000). The battery is ~10 years old (bought new 2016, little used).
  `max170xx_battery/cycle_count` is NOT cycles: the driver passes the MAX17047 `Cycles` register through, which
  counts in 1% of a full cycle and resets when the gauge powers up (datasheet), so 279 = 2.79 cycles since the
  gauge last lost power. `charge_full` (7.7 Ah > design 6.7 Ah) is a learned estimate, not a health figure.
- `/usr/local/bin/charge-limit` sets 4.144 V (~80% on such cells) and makes the file wheel-writable;
  run by `charge-limit.service` at boot (enabled by hand, waits up to 60 s for the charger) and by
  `90-charge-limit.rules` when the driver (re)registers; both log to `journalctl -t charge-limit` /
  `-u charge-limit`. At boot only the service applies it (checked at two boots, 2026-10-12): udev never
  processes the power_supply add events then (no USEC_INITIALIZED on any of them), so the rule
  alone left 4.384 V; a manual `udevadm trigger` does run it. The driver
  keeps the value and rewrites it on resume. Voltage is what stresses the cells
  (with heat), so this limits wear and swelling directly; no daemon, the chip stops by itself.
- Charge-path: the bq24292i powers the system from the adapter; once charged the battery rests
  (measured 1.5 mA `current_now` at full on the 9 V PD charger), it is not cycled while plugged in.
- Above the limit (just after lowering it) the battery runs the system until it is down to the limit
  (measured -0.6 A at 4.30 V), and the charger says "Not charging", or "Charging" (seen after a
  recharge cycle started, -1.2 A while it said so): the bar shows `bat N% max` either way.
  Once at the limit the charger says "Full" (at 89%, 4.185 V, +2 mA: the adapter runs the system,
  the battery rests; measured 2026-10-12); the bar shows `bat 89% max` (`bat full` only at >=99%). 
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

## Package power limit (RAPL)
- Firmware: PL1 6.0 W (1 s), PL2 20 W (10 s) (`/sys/class/powercap/intel-rapl:0/constraint_*`). CPU and GPU share it:
  in a GPU-heavy game the package sits at 6 W, and both clock down (GPU ~460 MHz, CPU down to 1.76 GHz) at only
  62-71 C. 8 W raised Dark Souls 24 -> 28 fps, temperatures 77-80 C (users/robin/docs/steam-library.md). PL1 and the GPU
  minimum clock are wheel-writable (`/etc/tmpfiles.d/turbo.conf`). Per game in shelf (W power limit, P pin, T/G
  CPU/GPU turbo, L fps cap; docs/frontend.md), which passes GPD_TDP_W, GPD_GPU_PIN, GPD_CPU_TURBO, GPD_GPU_TURBO. game-wrap sets them for the game's lifetime and puts
  PL1 6.016 W, the GPU minimum (RPn) and the previous turbo states back when it ends; a detached guard (`tail --pid`) does it even if
  game-wrap is SIGKILLed (tested: normal exit, game segfault, wrapper SIGKILL). Used by Dark Souls.
- Boot race (fixed 2026-10-12): tmpfiles applies turbo.conf at ~8 s, intel_rapl creates the powercap files at
  ~13 s, so PL1 and energy_uj kept their root-only modes (GPD_TDP_W silently ineffective, MangoHud 0 W).
  `turbo-perms.service` (enabled) waits for the files and runs tmpfiles for turbo.conf again.
- Reference points (2026-10-12): idle with the screen on ~1.2 W; Chrono Trigger (SNES, RetroArch snes9x, turbo
  off) draws ~2.1 W at full speed (CPU 1.6 GHz, RetroArch half a core, GPU at its minimum) and at a 1 W limit
  runs ~40 fps instead of 60 (two-thirds speed); Dark Souls wants 8 W.

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

## Charging voltage (USB-PD): 5 V only (2026-10-13)
- GPD specifies a 5 V / 2.5 A charger. The kernel's `chtwc_int33fe` (the INT33FE Type-C setup for the GPD Win and
  Pocket) gives tcpm the sink PDOs 5 V / 400 mA fixed and 5-12 V / 3 A variable, so a USB-PD charger supplied
  9 or 12 V (seen on 2026-10-12/13: 9 V and 12 V, `tcpm-source-psy-i2c-fusb302` `voltage_now`). Whether that
  played a part in the board fault below is unknown; nothing needs more than 5 V (2.5 W idle, ~8 W in games,
  ~4.4 W charging).
- Fix: DKMS package `gpd-usbpd` (`kernel/usbpd/`, built by `kernel/prepare.sh gpd-usbpd`, installed by
  `scripts/sync install`): the same driver with one sink PDO, 5 V fixed at up to 2.5 A. It loads from
  `/updates` instead of Arch's module (and from the initramfs after `mkinitcpio -P`). Not upstreamable as is:
  the Pocket has the same DMI strings and ships a 12 V charger. The stock boot entry uses Arch's module (12 V).
- Check after installing (not yet done, the board is out for repair): `modinfo -n intel_chtwc_int33fe` under
  `updates/dkms/`; with a 12 V-capable PD charger `cat /sys/class/power_supply/tcpm-source-psy-i2c-fusb302/voltage_now`
  = 5000000 and a USB meter at 5 V; the charger's `input_current_limit` should follow the 2.5 A contract (check).

## Overheating with a 4 A battery drain (2026-10-12, open)
After ~10 min of Wind Waker in Dolphin (package limit 6 W, MangoHud showed ~4 W) the device got very hot.
Dolphin stopped, charger unplugged, screen at 1%: package 1.3 W (normal), but the battery discharged at
3.9-4.1 A at 3.57 V (~14 W; normal idle 0.6 A / 2.6 W, table above). The gauge's own count agreed
(34 mAh in 30 s; 76% -> 54% in ~25 min) and the cell sagged 0.45 V below its open-circuit estimate, so
the load was real. PNIT 79 -> 81 C and rising while idle, coretemp 79 C at idle. The charger was not
in OTG/boost mode and USB-C was not sourcing. So ~11 W went somewhere outside the SoC: suspect a short,
a failing power component or the battery. Powered off to cool. Next: check the battery for swelling,
then boot on the charger and measure the drain right after boot.
After cooling (22:40, ~45 min off): still 4.1-4.6 A right after boot, SoC 1.3-1.8 W, temps 42 -> 48 C in two
minutes at idle. Backlight works: setting it from ssh dimmed the screen (Robin confirmed), the PWM follows (level 5 = 13% duty,
debugfs), and levels 100 to 1 change the current by only ~100-300 mA, so the backlight is not the extra load. (The
brightness key and the wob bar were out of step with the real level after boot: the bar showed the minimum at
level 100 and brightness-down did nothing; not investigated yet.) On a
12 V PD charger the battery charged at +1.1 A, so the charger covered the extra load, but PNIT still climbed
48 -> 59 C at idle. Powered off. Web search (2026-10-12): no known GPD Win 1 fault with these symptoms; swollen
batteries are a known GPD Win problem; the battery sits in the bottom cover (6 small Phillips screws, two cables:
battery and fan), so it can be inspected without taking the board out. The gauge measured the 4 A through its
sense resistor, so the load is outside the cell (an internal cell short would not show there).
Robin: the heat came from the body (base), not the screen half: the board or the battery.
Chargers: GPD specified 5 V / 2.5 A for this device, and owners repeat "only use plain 1-2 A chargers" (no
source found). Linux negotiates USB PD up to 12 V / 3 A here: `drivers/platform/x86/intel/chtwc_int33fe.c` gives
the fusb302 the sink PDOs 5 V fixed + 5-12 V variable at 3 A (written for the GPD Win and GPD Pocket, which share
this charger design; the Pocket ships with a 12 V PD charger). The device ran at 9 V PD and 12 V PD on
2026-10-12. Whether the Win 1's VBUS side tolerates that long-term is unknown; it may be unrelated to the fault.
Until that's known: plain 5 V chargers only.
USB power meter in the cable: ~20 W while on and charging (2 W SoC/screen + ~4 W charging + the ~14 W fault),
2 W while off (charging the battery while off). So the extra load exists only while the device is on: a rail
the board switches on with the system, not the always-on charger/battery side.
Battery disconnected ~5 min (gauge reset: cycle_count 2, charge_full back to design): idle drain at 5%
brightness 1.65 A (~6.0 W at 3.63 V; normal 0.59 A / 2.6 W), full brightness +0.25 A, package 1.15 W, temps
38 -> 42 C over 4 min. So the extra load fell from ~12 W to ~3.5 W but didn't go away. Next: battery out
overnight, then measure again.
Board: WIN155-V1.0-160805 (date code 1640). Robin found the hottest area by touch: the power entry next to the
battery connector (BAT): the charger IC (TI bq24292i, driven by the kernel's bq24190 driver), a large power
inductor left of it and small regulators/MOSFETs; the USB-A, HDMI and microSD sockets sit just above. Unidentified
yet which part. Since the load exists only while the device is on, a regulator in that cluster that the system
switches on (e.g. a 5 V rail for USB-A/HDMI/fan) or a short on its output fits better than the charger, whose
path is also live while off.
Close-up of that area: charger bq24292i (QFN), its 1.0 uH inductor ("1R0"), the MAX17047 gauge ("17047"), the
gauge's 10 mOhm sense resistor ("R010") at the battery connector, two small WLCSP chips (unidentified; one may be the
fusb302) and passives; no 5 V converter there. So the hot area is the charger circuit itself. No visible burn
marks. Candidates: the bq24292i failing (internal leakage; replaceable QFN-24 4x4 by a micro-soldering shop), or
heat from the battery pack, which lies over this area when the cover is closed. Pinpointing needs a thermal camera
or a careful fingertip test of the charger chip, inductor and gauge with the device on.
Robin (2026-10-13): no heat from the battery; the hot spot is the bare area below the BAT label, where dense via
fields lead to the other side of the board. The parts are therefore on the far side, under the copper plate:
SoC, PMIC, memory or their regulators (matches the SoC's own DTS sensors reading 76-79 C at a nominal 1.3 W).
Software test for next boot: does the extra current follow the CPU/GPU voltage (cap CPU at its lowest clock,
offline cores 1-3, GPU at its minimum, then turbo)? Leakage in the SoC or on its core rail scales with voltage;
a short elsewhere wouldn't.
Timeline: earlier on 2026-10-12, Dark Souls ran for hours at 8 W (CPU and GPU turbo, 77-80 C) while charging at
9 V PD. The device "almost melted" during the first Wind Waker test (~21:40-21:50), while charging. During that
test MangoHud showed ~4 W package yet the SoC sensors reached 85 C, hotter than Dark Souls at 8 W, so the extra load
was probably already there when Dolphin started; the long hot sessions before it may have tipped an ageing part.
No battery-current log exists from before 21:50 to show when it began.
After the battery was out overnight (2026-10-13, cold boot on battery, 5% brightness, idle): 1.5-2.1 A from
the first minute (typically ~1.6 A, ~5.8 W; normal 0.59 A), package 1.15-1.4 W, PNIT and DTS 34 -> 40 C in 2 min
and still rising. The same as right after the disconnect: the overnight rest cleared nothing more. The extra
~1 A (~3.5-4 W outside the package estimate) is there cold, so it is not heat-driven runaway.
USB meter cross-check (12 V PD, idle, battery charging at its 1.02 A setting): 10 W at the input, which fits
the gauge (~6 W system incl. the fault + ~4.4 W into the battery). The gauge is trustworthy. While charging the
battery current stays at the charge setting whatever the load, so load tests must run on battery.
Load test (same boot, charging at 12 V, Robin reading the meter): idle ~10 W in; 4 cores at 2.4 GHz (package
5.3 W) and Wind Waker in Dolphin (package 2-4.5 W, 90 s) peaked at ~16 W, i.e. input followed the package power
plus converter losses and the extra load did not grow. DTS reached 60 C (CPU) and 64 C (Dolphin), against 85 C in
the failing run on 2026-10-12. PNIT rose faster than the die (54 -> 75 C under CPU load), so it sits near the
SoC or its regulators. Afterwards idle was back at the same level. The fault did not escalate under load today.
Clock/voltage test on battery (same boot, unplugged, 5% brightness; "rest" = battery power minus RAPL package):
idle 5.95 W (pkg 1.28, rest 4.68); 1 core at 480 MHz, GPU 200 MHz 5.85 W (rest 4.42); 4 cores busy at 480 MHz 6.34 W
(rest 4.98); 4 cores busy at 2.4 GHz 10.98 W (pkg 5.37, rest 5.61); restored idle 5.88 W (rest 4.66). Normal
idle rest is ~1.4 W (2.56 W battery, 1.2 W package), so the fault is ~3.2 W. It did not move with the CPU's
clock or voltage: the +0.9 W in rest at 2.4 GHz is about the regulator loss and memory traffic of 4 W more package
power. So probably not SoC leakage: a part on another rail (PMIC output, memory, a regulator or a capacitor on
a rail switched on with the system), near PNIT. Next step is hardware (thermal camera or repair shop).
Robin (2026-10-13): a faint burnt smell from the photographed side (BAT area), also noticed on 2026-10-12. So a
part has overheated, not just aged. Decision: don't use or charge it; battery disconnected until repaired.
Far side of the hot area (Robin's photo, 2026-10-13), identification from markings, unconfirmed: HC3F3210A
(LQFP-64, HDSC logo, a microcontroller with its crystal; probably the gamepad controller), SH61F83Q (Sinowealth
QFN-48 microcontroller, probably the keyboard), two "3401" SOT-23 (P-channel MOSFET, e.g. AO3401: load switches or
rumble drivers), a "263DC" SOT-23-5 (probably the regulator for these chips). This is the input board area, on a
rail powered only while the system is on, and none of it is the SoC: matches the clock test. A microcontroller in
latch-up (high current, cleared by removing power, can leave lasting leakage) would fit the 4 A -> 1.6 A drop after
the battery disconnect. No burn mark visible in the photo; the pad by MIC+/MIC- is worth a close look.
Close-ups (Robin, 2026-10-13): the SH61F83Q has an irregular dark pit/blister on its top, over the date code
("1625"), off-centre and unlike the moulded pin-1 dot. That is what a die that overheated locally looks like
(the package blisters over the hot spot). Next to its left side a cracked translucent blob on the board between
the chip and the capacitors (melted residue or flux; not seen elsewhere). HC3F3210A, both 3401s, 263DC and the MIC
area (an empty 8-pad footprint there is unpopulated, not burnt) look normal. Main suspect: the SH61F83Q
(Sinowealth microcontroller), shorted or latched internally. Replacement needs a chip programmed with GPD's
firmware (or a donor board); removing it would stop the drain at the cost of what it drives.
Web (2026-10-13): SH61F83 = Sinowealth low-speed USB microcontroller (8051 core, QFN-48, for USB keyboards),
runs from 4.4-5.25 V, i.e. the 5 V rail, with its own 3.3 V regulator out. So it is the keyboard controller.
Win 1 forum reports: keyboards dying on Z8750 units; one "keyboard, fan and USB port stopped working overnight"
(gbatemp 480885), one keyboard that worked again only after changing the fan setting (pyra-handheld GPD Win
thread p. 92), metallic thermal paste near resistors blamed for shorts. Suggests keyboard, fan and USB-A share
a 5 V switch on this board (the 3401s?). No report of this chip burning found.
Smell test by a second person (2026-10-13): the SH61F83Q smells most burnt of all parts. Culprit taken as found.
Plan: no more powering up as is; a repair shop removes the chip (and checks its 5 V supply parts), then test the
drain; a programmed replacement chip from GPD or a donor board for the built-in keyboard.

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
To revert: remove `mitigations=off` from `devices/gpd-win1/system/boot/loader/entries/arch.conf`, `scripts/sync apply`.

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
