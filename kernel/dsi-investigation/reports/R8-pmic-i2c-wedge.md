# R8: PMIC I2C bus wedge during the screen-on stress test (2026-10-12 07:51)

Analysis of boot `4aa59a66ec6b4a8fb894a9d02cd7aa6f` (07:49:44-08:01:24) against kernel 7.2.9 sources
(`linux-7.2.9` tarball; i915 from `kernel/i915/build/src`), the device's sysfs/journal (read-only) and
upstream history. Facts are marked with file:line or log timestamps; everything else is hypothesis.
Line numbers are v7.2.9 unless noted.

Status of the experiments (section 6): prepared but **not run**. The tool-permission classifier of this
session refused GPU load, screen cycling and writing a script on the device, even after the owner's
go-ahead relayed by the main session. `/tmp/gpuload` is built and Arch's Mesa is unpacked at
`/tmp/mesa-stock` on the GPD (both gone at the next reboot); the runner script is in 6.1.

## 1. What the log says (facts)

Timeline (journal `-o short-precise`, boot 4aa59a66):

| time | event |
|---|---|
| 07:49:44 | boot (patched i915 DKMS srcversion 051DA60BA67259721D07E17 = new 0015; Mesa 1.5) |
| 07:49:51.9 | `charge-limit[366]: boot: 4144000 uV` (one write to the bq24190 via the PMIC bus) |
| 07:51:05.89 | `systemd-run ... screen-race-test.sh 300` started (userspace journal) |
| 07:51:05.97, 09.44, 12.92, 16.40 | `usb 1-3: USB disconnect` = screen off #1-#4 (period 3.47 s; the 2026-10-10 run had the same 3.48 s) |
| 07:51:18.66 | first `i2c_designware 808622C1:05: controller timed out`, then `*ERROR* mipi_exec_pmic failed, error: -110` |
| 07:51:18.66-07:51:39.59 | 2 failures per screen-on, 1 per screen-off, ~1.0-1.2 s apart (= the adapter timeout, see 2.3) |
| 07:51:42.7 | `bq24190-charger i2c-bq24190: Can't read SS reg: -110` (charger IRQ thread) |
| 07:52:01.7 | `leds platform::indicator: Failed to read LED CTRL reg: -110` (something reads the LED brightness every 2-5 s) |
| 07:52:23.2 | one DPI FIFO underrun + automatic resync (patch 0012 still working: the pipe/DSI link runs, the panel is dark) |
| 07:52:24.5 | the test unit stopped by hand |
| 07:52:24 - 08:01:24 | every PMIC-bus transfer keeps timing out (519 "controller timed out" in this boot) until the reboot |

The gamepad kept disconnecting/reconnecting on every cycle during the wedge (07:51:21.0, 27.6, 34.1 ...),
so its power is tied to GPIO 72 (panel reset, toggled by the GPIO elements of POWER_ON/OFF), not to the PMIC
write. The panel stayed black, which is what you expect if the PMIC write that enables its rail (2.1) never
reached the PMIC.

Messages that are **absent** (all 74 boots since 2026-10-06, including this one):
`I2C bus managed by PUNIT`, `P-Unit semaphore timed out`, `couldn't acquire bus ownership`,
`timeout waiting for bus ready`, `timeout in disabling adapter`, `timeout while trying to abort`,
`controller active after xfer`, `transfer terminated early`, any TX-abort/arbitration message,
`VLV_IER ... restoring` (0 in the 4 cycles before the wedge), `vblank wait timed out`, `flip_done`.

First failing transfer: screen-on #4 started at ~07:51:17.4 (off #4 at 16.40 + `sleep 1`); the first
timeout is logged 1.26 s later, i.e. the first transfer that timed out was (most likely) the first PMIC
read-modify-write of that POWER_ON sequence, or a transfer immediately before it (a charger/PMIC-IRQ read).
The log cannot tell which of the two, nor whether the bus died *during* that transfer or just before it.

Comparison boot 2026-10-10 (`3b856d11...`, 20:06:53-08:38): same script, 300 cycles 20:09:40-20:27:28,
old 0015, Arch's Mesa, gpuload `1420000 finishes, 1337/s`; 14 `VLV_IER` restores, 0 timeouts. The failing
run's gpuload rate was reported as ~1300-2900/s by the main session; `/tmp/racetest.log` did not survive
the reboot, so I could not verify it.

## 2. Mechanism

### 2.1 What the screen sequences do on the PMIC bus (fact)

`data/decode.txt` (igt `intel_vbt_decode`) **omits the PMIC elements**; decoding block 53 of `data/vbt.bin`
by hand (v3 block, size field 0, real size 249 in the header) gives:

- `MIPI_SEQ_POWER_ON` (62 bytes): GPIO 72 := 1; delay 20 ms; **PMIC i2c 0x6e reg 0x3c mask 0x01 value 0**;
  GPIO 72 := 0; delay 200 ms; **PMIC 0x6e/0x3c bit0 := 1**; GPIO 72 := 1.
- `MIPI_SEQ_POWER_OFF` (35 bytes): delay 20 ms; GPIO 72 := 0; **PMIC 0x6e/0x3c bit0 := 0**; delay 20 ms.
- DISPLAY_ON/OFF, BACKLIGHT_ON/OFF: DCS packets and GPIO 70 only.

So: 2 PMIC elements per screen-on, 1 per screen-off, exactly the 2:1 error pattern in the log.
Register 0x6e3c is `CHT_WC_V1P15_CTRL` (`drivers/acpi/pmic/intel_pmic_chtwc.c:18`), the Whiskey Cove's
1.15 V rail enable, next to `V1P05A_CTRL` (0x6e3b), the SoC's own 1.05 V rail.

Path: `vlv_dsi.c:771` `intel_dsi_pre_enable` -> `intel_dsi_vbt_exec_sequence(POWER_ON)` ->
`intel_dsi_vbt.c:551` `mipi_exec_pmic` -> `intel_pmic.c:339` `intel_soc_pmic_exec_mipi_pmic_seq_element`
(takes `intel_pmic_opregion->lock`) -> `intel_pmic_chtwc.c:234` `regmap_update_bits` = one SMBus byte read +
one byte write (`intel_soc_pmic_chtwc.c:72-110`, client address switched to 0x6e) on i2c-5 =
`i2c_designware 808622C1:05`. POWER_OFF runs from `intel_dsi_post_disable` (`vlv_dsi.c:928`).
Both run inside sway's modeset, with the display power domains held (not under `power_domains->lock`).

### 2.2 Who else uses i2c-5

- The PMIC's own I2C master (`i2c-cht-wc`, adapter i2c-15 "PMIC I2C Adapter", sysfs parent
  `.../i2c-INT34D3:00/cht_wcove_ext_chgr/i2c-15`) carries the bq24190 charger: every charger read is
  several PMIC register writes/reads on i2c-5 plus a wait for the PMIC interrupt (irq 120, ~0.6/s in steady
  state; the regmap-irq thread then reads `IRQLVL1`/`EXTCHGRIRQ` on i2c-5 too). Readers: `statusbar` (every
  60 s and on uevents), `inputd` (charger `online`), `low-battery.timer` (every 2 min, fuel gauge),
  `charge-limit` (once at boot), and some LED poller (`platform::charging`/`indicator`, every 2-5 s, seen
  only because it fails).
- `cht_wcove_leds`, `cht_wcove_pwrsrc`, `cht_wcove_region` (ACPI OpRegion, used by firmware `_PS0/_PS3`).
- **The P-Unit** (hardware): on BYT/CHT the SoC's power unit uses the PMIC bus for voltage changes. Kernel
  sources state this directly: `arch/x86/platform/intel/iosf_mbi.c:337-341` "Disallow the CPU to enter C6
  or C7 state, entering these states requires the P-Unit to talk to the PMIC and if this happens while we're
  holding the semaphore, the SoC hangs"; `drivers/gpu/drm/i915/intel_uncore.c:2339-2345` "a forcewake ...
  on systems where this notifier gets called requires the punit to access to the shared pmic i2c bus ...
  leading to 'render: timed out waiting for forcewake ack request'". GT forcewake/RC6 exits, GPU frequency
  requests (`gt/intel_rps.c:827` `vlv_rps_set` -> `PUNIT_REG_GPU_FREQ_REQ`) and display power wells
  (`intel_display_power_well.c:1177-1212` `vlv_set_power_well` -> `PUNIT_REG_PWRGT_CTRL`) are P-Unit
  operations that may need PMIC writes.

### 2.3 How the kernel arbitrates with the P-Unit, and why it does not on this device (fact)

`drivers/i2c/busses/i2c-designware-baytrail.c:15-44`: at probe the driver evaluates the ACPI method `_SEM`
of the controller; only if it exists and is non-zero it logs `I2C bus managed by PUNIT`, sets
`dev->acquire_lock = iosf_mbi_block_punit_i2c_access`, `release_lock = iosf_mbi_unblock_punit_i2c_access`
and `shared_with_punit = true`. Then every transfer (`i2c-designware-master.c:880/916`) first takes the
P-Unit semaphore (`iosf_mbi.c:312-375`: waits for i915's `iosf_mbi_punit_acquire()` holders, calls the
PMIC-bus-access notifier so i915 takes `FORCEWAKE_ALL` for the duration, pins the CPU out of C6/C7, then
requests semaphore register 0x10e and waits up to 500 ms), and `shared_with_punit` also keeps the controller
out of runtime suspend (`i2c-designware-platdrv.c:222` `pm_runtime_get_noresume`;
`i2c-designware-common.c:995/1017`).

On the GPD Win: `CONFIG_I2C_DESIGNWARE_BAYTRAIL=y` (`/proc/config.gz`), `808622C1:05` is ACPI
`\_SB_.PCI0.I2C7` with the PMIC `INT34D3:00` = `\_SB_.PCI0.I2C7.PMI5` under it, **no boot ever logged
"I2C bus managed by PUNIT"**, and the controller is runtime-suspended right now
(`/sys/bus/platform/devices/808622C1:05/power/runtime_status` = `suspended`, `control` = `auto`), which is
impossible with `shared_with_punit`. Conclusion: this firmware's DSDT has no (or a zero) `_SEM` on I2C7,
so **the kernel never takes the P-Unit semaphore, i915 never holds forcewake around PMIC transfers, and the
CPU is not kept out of C6/C7 during them.** Every PMIC transfer of the kernel is unarbitrated against
whatever the P-Unit does on the same bus. (Whether this GPD board routes the P-Unit's PMIC traffic over the
same wires cannot be read from sysfs; the semaphore register exists in the SoC regardless of firmware. The
DSDT itself is root-only: `/sys/firmware/acpi/tables/DSDT` 0400.)

i915's side of the arbitration (`vlv_iosf_sb.c:32-58` `iosf_mbi_punit_acquire/release` around every
Punit sideband access, `iosf_mbi.c:214-245`) only blocks the *kernel's* I2C transfers while i915 talks to
the P-Unit, and only if the I2C driver participates, which it does not here. So it is inert on this device.

### 2.4 What "controller timed out" means in 7.2.9 (fact)

`i2c-designware-master.c:719-741` `i2c_dw_wait_transfer`: waits `adapter.timeout` for the completion that
the ISR signals on STOP_DET or TX_ABRT; `adapter.timeout` is the i2c-core default `HZ` = 1 s
(`i2c-core-base.c:1573`). On expiry (`:766-781`): "controller timed out", `i2c_recover_bus()` (a no-op here:
`i2c_dw_init_recovery_info` `:979-1015` needs an `scl` GPIO, which ACPI/LPSS does not provide, so
`bus_recovery_info` stays NULL), `i2c_dw_init()` (disable + reprogram the controller), return -ETIMEDOUT.
i2c-core retries only on -EAGAIN (`i2c-core-base.c:2289-2297`), so the caller sees -110 after ~1.0-1.2 s
(plus the 1.1-22 ms `i2c_dw_wait_bus_not_busy` poll, `i2c-designware-common.c:752-772`, which only looks
at the controller's own ACTIVITY bit, not at the SDA/SCL lines).

What it is *not*: not the P-Unit semaphore (that path logs "P-Unit semaphore timed out"/"couldn't acquire
bus ownership" and is not compiled in on this device), not an abort (TX_ABRT sets `cmd_err`, no "timed
out"), not a controller that refuses to disable ("timeout in disabling adapter" is absent). The controller
disables, re-initialises and accepts the next transfer every time; it just never sees a STOP or an abort.
That is the signature of (a) a slave holding SCL low (clock stretch forever) / a bus left mid-transaction
by another master, or (b) the controller's interrupt never arriving, or (c) the controller's functional
clock stopped. The dark panel (its 1.15 V rail never re-enabled) argues that the writes really did not
execute, i.e. (a) or (c) rather than (b). Nothing in 7.2.9 can recover any of these without a reset; a
reboot resets the LPSS controller and (via firmware) the PMIC's I2C engine, which matches "only a reboot
recovered it".

### 2.5 Could patch 0015 (old or new) be involved?

Order of operations at screen-on (`intel_display_power_well.c:1348-1353`):
`vlv_set_power_well(true)` (P-Unit write + poll, `iosf_mbi_punit_acquire` held, `:1177-1212`) ->
`vlv_display_power_well_init` (`:1287-1333`): DPLL ref clocks, clock gating,
`valleyview_enable_display_irqs` (`:1312`) **= where 0015's `vlv_ier_settle` runs**, HPD init ->
return; later the encoder's pre_enable runs the POWER_ON sequence with its PMIC writes.
At screen-off: `vlv_display_power_well_deinit` (`:1335`, 0015's disable-side settle, then the stock
`intel_parent_irq_synchronize`) -> `vlv_set_power_well(false)`.

Both versions of 0015 run under `power_domains->lock` (a mutex that the PMIC path never takes), after the
P-Unit access has been released and before any I2C transfer; they touch only `VLV_IER` MMIO and
`display->irq.lock`; `synchronize_irq()` only waits for the i915 handler, which does no I2C. There is no
lock, forcewake or P-Unit request that the designware/PMIC path depends on. In the failing boot no
"restoring" line was logged, so the new loop ran one `synchronize_irq` + one read and returned; the old
version did one `synchronize_irq` + read + unconditional rewrite. The timing difference is one MMIO write
(sub-microsecond). I find **no mechanism** by which 0015 (either version) can wedge the PMIC bus; at most
it shifts the sequence by microseconds relative to the P-Unit's power-well work, which is noise against the
seconds of jitter in the test loop.

### 2.6 Could Mesa 1.5 matter?

`gpuload.c` uses a surfaceless EGL/GLES2 context, one 256x256 RGBA4 renderbuffer, `glClear` + `glFinish`.
Mesa patches 0001-0005 change crocus staging textures (snooping), large tiled uploads (GPU blit, 256 KiB
threshold), tiled-texture readback and buffer map invalidation; the driconf trim removes native-app
workarounds. None of these paths is exercised (no texture uploads, no readbacks, no buffer maps, no
driconf entry for `gpuload`). The only way Mesa can matter is **throughput**: a different finishes/s means a
different number of GT idle/active transitions and RPS requests per second, i.e. a different rate of
P-Unit PMIC traffic (2.2). The 10-10 run did 1337/s; if the failing run really ran at up to ~2900/s, that
doubles the collision opportunities. Unverified (6.2 E3 measures it).

### 2.7 A pre-existing race?

Everything needed for a collision has been there since the install: no `_SEM`, PMIC writes in every panel
power sequence, charger reads, and a P-Unit that uses the bus for GT/display voltage work. What the stress
test adds is the combination of (1) 3 PMIC read-modify-writes every 3.5 s, (2) display power well
on/off (P-Unit) every 3.5 s, (3) a GPU that completes 1-3 k tiny requests per second, so the GT bounces
between idle and active and RPS keeps re-requesting frequencies (= thousands of potential P-Unit PMIC
accesses per second, far more than a game that keeps the GT busy at a steady clock). The 6 days without an
error (incl. 300 clean cycles on 10-10) say the per-transfer collision probability is small; 4 cycles on
10-12 say it is not negligible. With one event the two runs are statistically compatible with the same
underlying probability; nothing in the logs distinguishes "bad luck" from "something got worse".

## 3. Hypotheses, ranked

1. **Unarbitrated P-Unit vs kernel PMIC bus collision (pre-existing), hit by the stress test's unusual
   P-Unit traffic.** For: no `_SEM` (fact); the only known trigger combines heavy PMIC traffic, display
   power well cycling and a GT that toggles idle/active thousands of times per second; kernel comments
   document exactly this failure class (forcewake/C-state vs PMIC bus) and Hans de Goede's semaphore work
   exists because collisions hang BYT/CHT SoCs. Against: the 10-10 run survived 300 cycles; normal use with
   steady charger/PMIC-IRQ traffic never hit it; no proof that this board's P-Unit shares the wires.
2. **Same mechanism, made more likely by a higher gpuload rate under Mesa 1.5** (more RC6/RPS churn).
   For: the reported 2900/s vs 1337/s. Against: unverified number; Mesa's patches do not touch gpuload's
   paths, so any rate change would be incidental (slim build, driconf).
3. **Something new since 10-10 in the PMIC traffic profile**: the charge limit (charger held at 4.144 V
   reports "Full"/"Not charging" and may toggle charge/recharge -> charger IRQs -> i2c-15 traffic), the
   LED poller. For: new on 10-12. Against: one-off/low-rate traffic; the wedge came 75 s after boot while
   the stress test ran, not during the previous 7 hours of this configuration (boot 1607f998 01:53-07:49).
4. **Patch 0015 rework.** No mechanism found (2.5); only timing noise. Lowest.
5. **Lost interrupt / clock-gated controller rather than a stuck bus.** Would explain "controller timed
   out" with a working wire, but not the dark panel. Low.

## 4. Known reports (what I could reach; lore/patchwork pages are behind Anubis and 403'd)

- Hans de Goede, 2017-2018: "x86: baytrail/cherrytrail: Rework and move P-Unit PMIC bus semaphore code"
  (e09db3d), "iosf_mbi: Add a PMIC bus access notifier" (528e649), "Add a mutex for P-Unit access"
  (9260a04), "i2c: designware-baytrail: Disallow the CPU to enter C6 or C7 while holding the punit
  semaphore", "i2c: designware-baytrail: Acquire P-Unit access on bus acquire", and i915
  264ec1a8 "Listen for PMIC bus access notifications" (fixes "render: timed out waiting for forcewake ack
  request", "punit semaphore timed out", "couldn't acquire bus ownership", followed by GPU/system
  freezes). These establish the P-Unit/PMIC-bus sharing on BYT/CHT and that it hangs when uncoordinated.
  https://patchwork.ozlabs.org/project/linux-i2c/patch/20181011142911.13750-2-hdegoede@redhat.com/ ,
  https://lkml.iu.edu/hypermail/linux/kernel/1810.1/04030.html ,
  https://lists.freedesktop.org/archives/intel-gfx/2017-January/115784.html (RFC "Coordinate pmic i2c bus
  and i915 punit accesses"), https://patchwork.kernel.org/patch/9737231/ , https://patchwork.kernel.org/patch/9899987/
- "i2c: designware: Disable pm for PMIC i2c-bus even if there is no _SEM method" (2017,
  https://patchwork.ozlabs.org/patch/738471/): upstream knows CHT devices whose PMIC bus has no `_SEM`
  (text not fetchable here); 7.2.9 handles that only via `ACCESS_NO_IRQ_SUSPEND`/system-suspend rules,
  not at runtime.
- "i2c: designware: Set IRQF_NO_SUSPEND flag for all BYT and CHT controllers" (2018,
  https://patchwork.kernel.org/project/linux-acpi/patch/20180919191518.18764-8-hdegoede@redhat.com/):
  `808622C1:06: controller timed out` when the PMIC is accessed from `_PS0` during resume_noirq with the
  controller's IRQ disabled: the lost-IRQ variant of this message (recoverable, ours is not).
- Ubuntu bug 1625440, kernel bugzilla 150911/155241, RH bz 1377534: "808622C1:06: punit semaphore timed
  out" on CHT devices *with* `_SEM` (the other failure mode of the same sharing).
- "drm/i915/vlv_dsi: Skip MIPI I2C sequences on Microsoft Surface 3"
  (https://www.spinics.net/lists/intel-gfx/msg291050.html): a MIPI I2C element hanging `808622C1:04`.
- "ERROR mipi_exec_pmic [i915]" (Linux Mint forum, error -95 = no PMIC driver) and the GPD win/pocket PMIC
  sequence support ("drm/i915/intel_dsi_vbt: Add support for PMIC MIPI sequences",
  https://patchwork.kernel.org/patch/10750337/): the PMIC element path exists *for* the GPD devices.
- Nothing after 7.2.9: `i2c-designware-master.c`/`-common.c` commits up to Aug 2026 are register
  definitions, target-mode handling, `I2C_M_STOP`, `IC_EMPTYFIFO_HOLD_MASTER`; no timeout/recovery fix.
  `intel_dsi_vbt.c` 2025-2026: BLLP/clock-stop options, logging. No report of a permanent PMIC-bus wedge
  from a display sequence on CHT was found.

## 5. Recommendation

- **Revert nothing on the evidence.** Neither 0015 nor Mesa 1.5 has a path to the I2C controller; both
  are at most timing/rate changes. If the user wants a belt-and-braces step anyway, run the stress test
  under stock Mesa first (E3 below) before touching 0015 (which needs sudo for a one-shot boot entry).
- **Treat the stress test as the hazard**: gpuload + screen cycling is the only known trigger. Do not run
  it without the abort wrapper below; expect a reboot when it trips.
- **Normal use**: low risk. Idle/lid screen-off freezes games first (`screen.sh`), so the GT is idle when
  the PMIC sequences run; the power button screen-on is the same path. The exposure is `Mod4+F10`
  (screen reset) while a game is rendering (GT active, RPS/RC6 transitions per frame, ~60/s, not
  thousands) plus the occasional charger read; six days of this had no event. If it ever happens outside
  the stress test, the symptom is the same (panel dark after a reset, `controller timed out` in the log,
  charger unreadable) and the only fix is a reboot.
- **If hypothesis 1 is confirmed** (E1/E3 reproduce without any display involvement, or with stock Mesa
  and either 0015): the proper fix is to make the kernel arbitrate: an SSDT overlay (initrd ACPI table
  upgrade) adding `Name (_SEM, One)` under `\_SB.PCI0.I2C7`, which switches on the stock mechanism (P-Unit
  semaphore, forcewake held by i915 during PMIC transfers, no C6/C7 while holding it, controller never
  runtime-suspended). Risks: ~1 ms extra per PMIC transfer, a few mW (controller stays on), and the known
  "P-Unit semaphore timed out" class of bugs on firmware that does not implement the semaphore; test with
  the stress loop + abort wrapper, keep a boot entry without the overlay. The driver is built in
  (`CONFIG_I2C_DESIGNWARE_PLATFORM=y`), so a DMI quirk would need a kernel rebuild; the SSDT route does not.
  A read-only precheck needs sudo once: `sudo cat /sys/firmware/acpi/tables/DSDT > dsdt.aml; iasl -d
  dsdt.aml; grep -n '_SEM' dsdt.dsl` (expect none under I2C7).

## 6. Experiments (cheapest and most discriminating first; none run yet)

All runs: abort at the first `controller timed out`/`mipi_exec_pmic`, kill the GPU load, leave the screen
on; a wedge needs `sudo -n systemctl reboot`. Log the gpuload rate. Reboot between a wedge and the next
run only (the bus is fine otherwise).

### 6.1 Runner (write to `/tmp/r8-cycle.sh` on the GPD; `/tmp/gpuload` and `/tmp/mesa-stock` are there now)

```bash
#!/bin/bash
# r8-cycle.sh LABEL CYCLES [OFF_S] [ON_S]   env: GPULOAD=1 runs /tmp/gpuload; MESA=/tmp/mesa-stock uses Arch's Mesa for it;
# NOSCREEN=1 skips the off/on (pure GPU load + PMIC polling); POLL=1 reads the charger 5x/s.
L=$1 N=${2:-60} OFF=${3:-1} ON=${4:-1.5}
export SWAYSOCK=$(ls /run/user/1000/sway-ipc.*.sock)
log=/tmp/r8-$L.log; since=$(date '+%Y-%m-%d %H:%M:%S'); G= P=
if [ "$GPULOAD" = 1 ]; then
  if [ -n "$MESA" ]; then
    LD_LIBRARY_PATH=$MESA/usr/lib __EGL_VENDOR_LIBRARY_FILENAMES=$MESA/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    LIBGL_DRIVERS_PATH=$MESA/usr/lib/dri /tmp/gpuload > /tmp/r8-$L-gpuload.log 2>&1 & G=$!
  else /tmp/gpuload > /tmp/r8-$L-gpuload.log 2>&1 & G=$!; fi
fi
if [ "$POLL" = 1 ]; then ( while :; do cat /sys/class/power_supply/bq24190-charger/status \
  /sys/class/power_supply/max170xx_battery/capacity >/dev/null 2>&1; sleep 0.2; done ) & P=$!; fi
cleanup() { [ -n "$G" ] && kill $G 2>/dev/null; [ -n "$P" ] && kill $P 2>/dev/null; G= P=; }
trap cleanup EXIT
bad() { journalctl -k --since "$since" --no-pager 2>/dev/null | grep -E "controller timed out|mipi_exec_pmic|semaphore|couldn't acquire|VLV_IER did not settle|vblank wait timed out|flip_done" | head -3; }
echo "start $since label=$L cycles=$N off=$OFF on=$ON gpuload=${GPULOAD:-0} mesa=${MESA:-system} noscreen=${NOSCREEN:-0} poll=${POLL:-0}" > $log
for i in $(seq 1 "$N"); do
  [ -e /tmp/r8-stop ] && { echo "stopped at cycle $i" >> $log; break; }
  if [ "$NOSCREEN" = 1 ]; then sleep 2.5; else
    swaymsg -q 'output DSI-1 power off'; sleep "$OFF"; swaymsg -q 'output DSI-1 power on'; sleep "$ON"; fi
  b=$(bad); if [ -n "$b" ]; then echo "ABORT at cycle $i $(date +%T): $b" >> $log; cleanup; swaymsg -q 'output DSI-1 power on'; break; fi
  (( i % 25 == 0 )) && echo "$(date +%T) cycle $i gt_cur=$(cat /sys/class/drm/card1/gt_cur_freq_mhz) rc6_ms=$(cat /sys/class/drm/card1/gt/gt0/rc6_residency_ms)" >> $log
done
cleanup
echo "end $(date +%T) after $i cycles; gpuload: $(tail -1 /tmp/r8-$L-gpuload.log 2>/dev/null)" >> $log
journalctl -k --since "$since" --no-pager -o short-monotonic | grep -E "VLV_IER|timed out|mipi_exec|flip_done|DSI resync" >> $log
```
Start detached: `systemd-run --user --unit=r8-LABEL -E GPULOAD=1 bash /tmp/r8-cycle.sh LABEL 300`; stop early
with `touch /tmp/r8-stop`. Record every run (setup, cycles, outcome, gpuload rate, `rc6_residency_ms`
delta) in this file.

### 6.2 Order

- **E1 (no display involvement)**: `GPULOAD=1 NOSCREEN=1 POLL=1`, 240 "cycles" (10 min): GPU churn + 5 PMIC
  reads/s, screen untouched. Wedge -> hypothesis 1 confirmed and 0015/Mesa are out of the picture (the
  panel sequence was never run). Clean -> the P-Unit traffic that matters is tied to the display power
  well / panel sequence, continue with E3.
- **E2 (display only)**: no GPU load, 300 cycles. Expected clean (daily use). A wedge here would be new and
  would point at the power-well/P-Unit side of the sequence itself (then compare old 0015, E4).
- **E3 (A/B Mesa)**: `GPULOAD=1` with `MESA=/tmp/mesa-stock` for 100 cycles, then system Mesa for 100,
  alternating; compare finishes/s and `rc6_residency_ms` growth per 100 cycles. If only the 1.5 runs wedge,
  or 1.5 runs at a clearly higher rate, hypothesis 2. If both wedge, hypothesis 1 and the rate is the knob.
- **E4 (A/B 0015)**: only if E1-E3 leave 0015 as the difference: build the old 0015 into a test module and
  boot it one-shot (`kernel/i915/install-test.sh`, needs sudo), repeat E3's system-Mesa run.
- **E5 (read-only, sudo once)**: dump the DSDT and confirm `_SEM` is absent (section 5); check whether
  `I2C7` has `_PS0/_PS3` that touch the PMIC (`PMI5`) - that is Hans's resume_noirq case and relevant to
  the suspend path.
- **Instrumentation that costs nothing**: `cat /sys/class/drm/card1/gt/gt0/rc6_residency_ms` before/after
  a gpuload minute tells whether the GT actually enters RC6 between finishes (RC6 exits are the forcewake
  -> P-Unit -> PMIC events); `gt_cur_freq_mhz` sampled at 1 Hz shows RPS churn.

## 7. Where the sources are

Kernel 7.2.9 extracted in this session's scratchpad
(`/tmp/claude-1000/-home-robin-Code-gpdwin/*/scratchpad/src/linux-7.2.9`); the DKMS i915 tree in
`kernel/i915/build/src/drivers/gpu/drm/i915` differs from upstream only by our patches
(`intel_dsi_vbt.c` identical, `vlv_dsi.c` = 0012-0014/0016-0018). Journal dumps of the failing boot were
copied to the scratchpad (`logs/r8-kern.log`, `logs/r8-all.log`), nothing was written to the repo's data.

## Addendum (main session, 2026-10-12 08:35): E5 result contradicts hypothesis 1

DSDT dumped (`sudo cat /sys/firmware/acpi/tables/DSDT`, `iasl -d`): `\_SB.PCI0.I2C7` (`808622C1`, `_UID 7`) **has** a
`_SEM` method, but it returns 1 only for `PMID == 1 || PMID == 2` (X-Powers `INT33F4` / TI `INT33F5`, devices PMI1/PMI2)
and 0 otherwise. This GPD has the Whiskey Cove PMIC `INT34D3` (device PMI5, `_STA` = 0x0F only for `PMID == 3`), so the
firmware deliberately declares the bus **not** shared with the P-Unit. Likewise I2C7's `_PS3` returns early (keeps the
controller powered) only for PMID 1/2; for Whiskey Cove it really puts the controller in D3 (`PSAT |= 3`), i.e. the
controller is runtime-suspended between transfers by design. So: the missing semaphore is not an omission, an SSDT
`_SEM=1` overlay would contradict the firmware (not to be done), and a P-Unit collision on this bus is unlikely.
Remaining candidates: something in the controller's runtime suspend/resume or the kernel side of the transfers
(e.g. a lost controller interrupt around a resume), or the PMIC itself (Whiskey Cove) misbehaving under the panel
power sequence. The failing boot shows no "nobody cared"/disabled IRQ. Experiments continue (E1 running).

## Experiments (main session, 2026-10-12, boot from 08:01, i915 with the reworked 0015, Mesa 1.5; `scripts/r8-cycle.sh`)

| run | setup | result |
|---|---|---|
| E1 08:38-08:48 | gpuload (1271 finishes/s) + charger/battery reads 5/s, screen untouched, 240 x 2.5 s | clean, 0 I2C errors |
| E2 08:48-09:06 | screen off/on, no GPU load, 300 cycles | clean |
| E3a 09:06 | gpuload on Arch's stock Mesa (1306/s) + screen off/on, 100 cycles | clean |
| E3b 09:12 | gpuload on our Mesa 1.5 (1349/s) + screen off/on, 100 cycles | clean |
| E3c 09:18 | stock Mesa (1329/s), 100 cycles | clean |
| E3d 09:24 | our Mesa 1.5 (1318/s), 100 cycles | clean |

During E3 the reworked 0015 restored a lost VLV_IER 24 times on enable and 22 times on disable, every time settled
at once (no "did not settle", no vblank/flip timeouts). The gpuload rate is the same with both Mesas and as on
2026-10-10 (1337/s), so Mesa 1.5 does not change the load. The charge-limit boot service writes through the charger
driver (serialized adapter access) within seconds of boot, long before the failing test.

Conclusion: the wedge is not reproducible on demand: 400 cycles of the exact failing combination (200 with the same
i915 and Mesa as at 07:51) plus 300 display-only cycles ran clean. Neither 0015 nor Mesa 1.5 is implicated by any
evidence; nothing is reverted. The one untested difference is timing: the failing run started 80 s after boot (the
later runs 37+ min in). Open, low priority. Precaution: stress tests use `r8-cycle.sh` (aborts at the first I2C error)
instead of `screen-race-test.sh`; recovery from a wedge is a reboot.
