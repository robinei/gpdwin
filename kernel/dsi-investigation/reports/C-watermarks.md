# Report C: CHV display FIFO / watermarks, pipe underrun detection, MIPI interrupt handling

Source: pristine v7.2.9, `drivers/gpu/drm/i915/display/` (line numbers refer to it).
Panel: 720x1280, htotal 774, vtotal 1308, pixel rate 61000 kHz (61111 kHz read back), pipe B,
primary plane XR24 (cpp 4) X-tiled 720x1280, sprites off, cursor plane without fb in both dumps.
Reimplementation of the watermark code: `SCRATCH/work-C/vlv_wm.py` (run with `python3 -I`).

Summary verdicts
- Watermark / FIFO arithmetic for this panel: REJECTED-cleared (no arithmetic, unit, overflow or
  rounding bug; values below).
- DDL / PND deadline: hardcoded by design (commit 262cd2e154c2, "lie to the system agent"), cleared.
- Pipe B FIFO underrun (PIPEBSTAT bit 31) detection: works and is polled at every display/GT
  interrupt once reporting is enabled; "no kernel message" is valid evidence only after the first
  driver modeset. In the fresh-boot state (no modeset yet, the state of the 15:31:43 desync) pipe
  underrun reporting is DISABLED on CHV and underruns are cleared silently (by design, see item 2).
- MIPI_INTR_STAT: never read/cleared/handled except SPL_PKT_SENT/GEN_READ_DATA_AVAIL for command
  transfer. There IS a MIPI interrupt line into VLV_IIR (bit 19 `I915_MIPIC_INTERRUPT`) but it is
  masked (IMR) and not enabled (IER): harmless, no storm possible. Cleared.
- Portrait/high line rate: nothing in the watermark code mis-handles the 12.7 us line; margins are
  the generic ones (item 4). Cleared, with one PLAUSIBLE note about the PM5 level margin.

---

## 1. Watermarks, FIFO split, drain latency for this panel

### 1.1 Code path (what runs on CHV)
- `i9xx_wm.c:4157` `vlv_setup_wm_latency()`: PM2 = 3 us, PM5 = 12 us, DDR DVFS = 33 us (CHV),
  `num_levels = 3`. Units: us, multiplied by 10 at `i9xx_wm.c:1542` -> 0.1 us units as
  `intel_wm_method2()` (`i9xx_wm.c:527`) documents. Consistent.
- `vlv_wm_get_hw_state()` (`i9xx_wm.c:3905`): if the Punit does not ack a DDR DVFS request within
  3 ms, `num_levels` is lowered to 2 (PM5). Not knowable from the dumps (drm_dbg only).
- `vlv_compute_wm_level()` (`i9xx_wm.c:1511`): `pixel_rate = crtc_state->pixel_rate`, on GMCH =
  `pipe_mode.crtc_clock` (`intel_display.c:2255`): 61000 at modeset time, 61111 after a hw readout
  (`vlv_dsi.c:1195`, `adjusted_mode.crtc_clock = pclk` from the DSI PLL). Identical results.
  `htotal = pipe_mode.crtc_htotal = 774`, `width = src width = 720`, `cpp = 4`.
- `intel_wm_method2()`: `ret = (latency * pixel_rate) / (htotal * 10000); ret = (ret+1)*width*cpp`.
  Overflow: max product 330 * 61111 = 20.2e6, htotal*10000 = 7.74e6; `unsigned int`, fine.
  `vlv_wm_method2()` then `DIV_ROUND_UP(bytes, 64)` -> cachelines.
- Cursor: hardcoded 63 cachelines (`i9xx_wm.c:1535`, FIXME in the code), FIFO fixed at 63
  (`vlv_compute_fifo`, `i9xx_wm.c:1611`), inverted to 0 when the cursor has an fb and 63 when not.
  `intel_wm_plane_visible()` (`intel_wm.c:136`) treats the cursor as visible iff `hw.fb != NULL`;
  both dumps show cursor B with `[FB:0]` -> raw 0, programmed 63.

### 1.2 Values for this panel (from `vlv_wm.py`, exact integer arithmetic)
Line time 774/61000 kHz = 12.689 us (line rate 78.8 kHz); line bytes 720*4 = 2880.

| level | latency | floor(lat/line) | bytes | cachelines (raw) |
|---|---|---|---|---|
| PM2 | 3 us | 0 | 1*2880 = 2880 | 45 |
| PM5 | 12 us | 0 | 1*2880 = 2880 | 45 |
| DDR DVFS | 33 us | 2 | 3*2880 = 8640 | 135 |

FIFO split (`vlv_compute_fifo`, `i9xx_wm.c:1554`): one active non-cursor plane -> total_rate = 45,
primary = 511*45/45 = 511 cachelines, sprites 0, cursor 63; `fifo_left` = 0.
DSPARB sprite C start = sprite D start = 511 (bit 8 in DSPARB2).

`_vlv_compute_pipe_wm` (`i9xx_wm.c:1742`): all three levels valid (135 <= 511), `cxsr = true`
(pipe B, exactly one plane). `sr_fifo_size = 3*512-1 = 1535`. Inverted (programmed) values:

| level | PLANEB | CURSORB (no fb / fb) | SR | CURSOR_SR (no fb / fb) |
|---|---|---|---|---|
| PM2 | 466 | 63 / 0 | 1490 | 63 / 0 |
| PM5 | 466 | 63 / 0 | 1490 | 63 / 0 |
| DDR DVFS | 376 | 63 / 0 | 1400 | 63 / 0 |

`vlv_merge_wm` (`i9xx_wm.c:2028`): one active pipe -> `level = DDR_DVFS`, `cxsr = true`. Registers
the driver writes (cursor without fb):
- `DSPFW1` (0x1f0034) = `0xbc3f7800` (SR low 9 bits = 1400 & 0x1ff = 376, CURSORB 63, PLANEB 376)
- `DSPFW3` (0x1f003c) = `0x3f000000`; `DSPFW7_CHV` (0x1f00b4) = 0; `DSPHOWM` (0x1f0064) = `0x02001000`
  (SR_HI = 1400>>9 = 2, PLANEB_HI = 376>>8 = 1)
- `VLV_DDL(B)` (0x1f0054) = `0x82828282`
- `FW_BLC_SELF_VLV` (0x186500) bit 15 set (maxfifo / CS power-down allowed), Punit `DSPSSPM`
  PM5 enabled, `DDR_SETUP2` FORCE_DDR_HIGH_FREQ cleared (DDR DVFS allowed) -- unless the Punit did
  not ack at boot, in which case level = PM5 and the PM5 values (466/1490) are used.
These registers are NOT in the dumps (`glitch-*-regs.txt` covers only the MIPI block, 0x1e17xx,
0x1e119x and the pipe B timing block). A read-only `intel_reg read` of 0x1f0034/0x1f0064/0x1f0054/
0x186500 would confirm; `drm.debug=0x4` prints "primary B watermarks: PM2=45, PM5=45, DDR DVFS=135".

Meaning: the plane starts refetching when 45 cachelines (= exactly 1 line = 12.69 us of scan-out)
remain in the FIFO at PM2/PM5, and when 135 cachelines (3 lines = 38 us) remain when DDR DVFS is
allowed. The FIFO holds 511*64 = 32704 B = 11.4 lines, so the plane FIFO can bridge 144 us
(or 1535 cachelines = 34 lines = 432 us in maxfifo mode) of memory stall before running dry.

### 1.3 Things checked and found OK
- Shifts/masks in `vlv_write_wm_values` (`i9xx_wm.c:838`): PLANEB 8 bits + HI bit -> 9 bits (max 511);
  SR 9 bits + 2 HI bits on CHV -> 11 bits (max 2047 >= 1535); cursor 6 bits (max 63). All fit.
  The "wtf" offsets `DSPFW7_CHV = 0x700b4` / `DSPFW9_CHV = 0x7007c` are mirrored in
  `vlv_read_wm_values` (readout matches write).
- `vlv_invert_wm_value()` and `vlv_raw_plane_wm_is_valid()` are consistent (raw <= fifo).
- DDL (`vlv_merge_wm`, `i9xx_wm.c:2062`): `DDL_PRECISION_HIGH | 2` for every plane: the driver
  deliberately reports a minimal deadline so the system agent treats all display requests as urgent;
  `CBR1_VLV` is written 0 (`intel_display_power_well.c:1279`), so the PND deadline mechanism stays
  enabled in hw but is neutralised by the DDL value. Commit 262cd2e154c2 ("CHV DDR DVFS support and
  another watermark rewrite"): "the VLV/CHV system agent doesn't understand memory latencies ... give
  up on the deadline scheme and program the watermarks old school". Intentional, not a bug.
- Trickle feed: on CHV the primary plane does not set `DISP_TRICKLE_FEED_DISABLE` (`i9xx_plane.c:165`
  is g4x/ilk/snb/ivb only; DSPBCNTR dump `0x98000400` = ENABLE | BGRX888 | TILED confirms bit 14
  clear). The whole display engine has trickle feed disabled through
  `MI_ARB_VLV = MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE_VLV` in `vlv_init_display_clock_gating()`
  (`intel_display_power_well.c:1277`). Consistent with how VLV/CHV is handled since 2014.
- cxsr handling around plane changes: `i9xx_must_disable_cxsr()` (`intel_plane.c:573`) forces cxsr
  off for primary/sprite enable/disable and for any DSPCNTR change other than TILED; cursor changes
  exempt. `vlv_compute_intermediate_wm` takes the min of old/new per level and disables cxsr in the
  intermediate state when `disable_cxsr`. Sway's per-frame flips only change DSPSURF -> no cxsr
  toggling per frame.
- No upstream change to the vlv/chv watermark code after v7.2 (`git log v7.2..HEAD -- i9xx_wm.c`
  shows only unrelated refactors: pixel_rate_cdclk, blend mode, pmdemand).
- Memory-bandwidth load test (`data/load-test.log`) causing no underrun is consistent with the
  comfortable plane-FIFO margins above.

### 1.4 Verdict
REJECTED-cleared: no bug in the vlv/chv watermark, FIFO split or DDL code for this configuration.

---

## 2. PIPEBSTAT 0x10040603 and pipe FIFO underrun detection

### 2.1 Decode (`intel_display_regs.h:930-978`)
| bit | value | meaning |
|---|---|---|
| 0 | 1 | PIPE_HBLANK_INT_STATUS / PIPE_OVERLAY_UPDATED_STATUS (status, never cleared by the driver: not in `pipestat_irq_mask`) |
| 1 | 1 | PIPE_FRAMESTART_INTERRUPT_STATUS (status, idem) |
| 2 | 0 | PIPE_START_VBLANK_INTERRUPT_STATUS (cleared by the IRQ ack each vblank) |
| 9 | 1 | PIPE_VSYNC_INTERRUPT_STATUS (status, idem bit 0) |
| 10 | 1 | PIPE_HOTPLUG_INTERRUPT_STATUS (status, idem) |
| 12 | 0 | PIPE_CRC_DONE_INTERRUPT_STATUS |
| 18 | 1 | enable for bit 2: START_VBLANK interrupt enabled (sway holds vblank on; `i965_enable_vblank`, `intel_display_irq.c:1708`) |
| 28 | 1 | enable for bit 12: CRC_DONE interrupt enabled on every pipe by `_vlv_display_irq_postinstall` (`intel_display_irq.c:2085-2089`) |
| 26 | 0 | PLANE_FLIP_DONE_INT_EN_VLV: flip-done interrupt not enabled at snapshot time |
| 31 | 0 | PIPE_FIFO_UNDERRUN_STATUS clear in both good and bad dumps |

Sticky status bits 0/1/9/10 stay set forever because the ack only clears bits in
`PIPE_FIFO_UNDERRUN_STATUS | pipestat_irq_mask[pipe]` (`intel_display_irq.c:559-574`). Harmless.

### 2.2 How bit 31 is checked on CHV
- No interrupt for it (DOC comment `intel_fifo_underrun.c:50`). It is polled inside
  `i9xx_pipestat_irq_ack()` (`intel_display_irq.c:533`), which `cherryview_irq_handler()`
  (`i915_irq.c:297` -> `intel_display_irq_ack` -> `vlv_display_irq_ack`, `intel_display_irq.c:2136`,
  "Call regardless, as some status bits might not be signalled in IIR") runs on EVERY interrupt of
  the device: GT (render completion), vblank, flip done, LPE audio, hotplug. `status_mask` always
  contains `PIPE_FIFO_UNDERRUN_STATUS`, so PIPESTAT is read for every pipe on every IRQ (skipped only
  when `vlv_display_irqs_enabled` is false = display power well off, impossible with the pipe on).
- If bit 31 is set: the ack writes `pipe_stats[pipe]` back (W1C -> bit 31 cleared), then
  `valleyview_pipestat_irq_handler()` (`intel_display_irq.c:666`) calls
  `intel_cpu_fifo_underrun_irq_handler()` (`intel_fifo_underrun.c:434`):
  - if `crtc->cpu_fifo_underrun_disabled` -> return silently ("GMCH can't disable fifo underruns,
    filter them"). The bit has already been cleared by the ack.
  - else: reporting is turned off for the pipe and `drm_err("CPU pipe %c FIFO underrun")` is printed
    once; nothing further is logged until the next modeset re-enables reporting.
- Additionally `intel_check_cpu_fifo_underruns()` is called at the end of every atomic commit
  (`intel_display.c:7613`) -> `i9xx_check_fifo_underruns()` (`intel_fifo_underrun.c:181`) reads the bit
  and prints "pipe %c underrun" (also only when reporting is enabled).
- With sway flipping at 60 Hz and a game generating GT interrupts, the bit is sampled hundreds of
  times per second; a latched underrun cannot be lost between samples (W1C, only the ack clears it).

### 2.3 When is reporting enabled on pipe B?
- Boot: `intel_sanitize_fifo_underrun_reporting()` (`intel_modeset_setup.c:447`) initialises
  `cpu_fifo_underrun_disabled = true` for ALL pipes on GMCH platforms (`!HAS_GMCH` -> false), with
  the comment that garbage would otherwise be reported.
- Enabled only by a full modeset: `intel_dsi_pre_enable()` (`vlv_dsi.c:738`) and
  `valleyview_crtc_enable()` (`intel_display.c:2081`); disabled again in `i9xx_crtc_disable()`
  (`intel_display.c:2182`) and by the first reported underrun.
- The device's own evidence shows the first sway takeover is a fastset, not a modeset: `MIPI_HS_TX_TIMEOUT`
  keeps the firmware value 0x3fffff until the first `output power off/on` (underrun log
  "start 15:30:14 ... 0x3fffff", changed to 0x55ff only after "output OFF/ON" at 15:39). A fastset
  does not call `pre_enable`, so in the fresh-boot state pipe B underrun reporting is DISABLED
  (`cpu=no` would be shown by `i915_display_info`) and any pipe FIFO underrun is cleared silently by
  the IRQ ack. The 15:31:43 desync (fresh boot, 0x3fffff) had no pipe-underrun coverage at all.
- The two dumps (`glitch-good/bad-i915_display_info.txt`) show `underrun reporting: cpu=yes` for
  pipe B and `0x55ff` in the register dump, i.e. taken after a modeset; for those states the
  "no CPU pipe B FIFO underrun message" is real evidence that the plane FIFO did not run dry
  between that modeset and the dump (as long as the kernel log was checked after the DPI underrun at
  15:39:57 / 15:46:54 with reporting still `cpu=yes`).

### 2.4 Other ways a pipe underrun could be missed (checked)
- `i9xx_set_fifo_underrun_reporting(enable=true)` (`intel_fifo_underrun.c:198`) clears bit 31 without
  reporting when reporting is (re)enabled at modeset start: intended.
- `i9xx_pipestat_irq_reset()` (`intel_display_irq.c:520`) clears it on display power well off/on and
  suspend/resume: not relevant while the pipe runs.
- `i915_enable_pipestat/disable_pipestat` write `enable_mask | status_mask` with bit 31 = 0, which does
  not clear a W1C bit. OK.
- After the first reported underrun on a pipe, all further ones are dropped until the next modeset
  (`cpu_fifo_underrun_disabled`). That would show as `cpu=no` in `i915_display_info`. Both dumps show
  `cpu=yes`, so no underrun had been reported.

### 2.5 Verdict
- Pipe B plane FIFO underrun did not occur between the last modeset and the dumps: supported by code
  (bit 31 = 0, `cpu=yes`, no message), provided the kernel log was checked after a DPI underrun that
  happened in the post-modeset state (15:39:57 and 15:46:54 qualify).
- "No kernel message" is NOT evidence for the fresh-boot state (reporting disabled by
  `intel_sanitize_fifo_underrun_reporting`, underruns cleared silently). Test-plan note: do one
  `output power off/on` after boot before a test session, or check `i915_display_info` `cpu=` before
  trusting the log.
- Not a bug: long-standing GMCH policy. A one-line debug patch could enable reporting on active GMCH
  pipes at boot after clearing the bit, but that is a test aid, not an upstream fix.
- Consequence for the theory: the DPI FIFO underrun (MIPI block, downstream of the pipe) occurs while
  the pipe's own plane FIFO is fine -> the pixel stream from the pipe to the DSI transmitter is the
  problem (clock ratio / packet timing / stalls in the DSI block), not memory fetch. Watermark changes
  are not expected to affect it.

---

## 3. MIPI_INTR_STAT / MIPI_INTR_EN handling and interrupt routing

### 3.1 Uses in the driver (v7.2.9, VLV/CHV path)
- `intel_dsi_prepare()` `vlv_dsi.c:1348-1349`: `MIPI_INTR_STAT = 0xffffffff` (clear all, W1C) and
  `MIPI_INTR_EN = 0xffffffff` (enable all) at every modeset, comment "XXX: why here, why like this?
  handling in irq handler?!". Nothing ever reads `MIPI_INTR_EN` again.
- `dpi_send_cmd()` `vlv_dsi.c:238-248`: clears and waits for `SPL_PKT_SENT_INTERRUPT` (bit 30).
- `intel_dsi_host_transfer()` `vlv_dsi.c:174-190`: clears and waits for `GEN_READ_DATA_AVAIL` (reads).
- `MIPI_INTR_STAT_REG_1 / MIPI_INTR_EN_REG_1` (0xb890/0xb894): defined, never used.
- No code anywhere reads, clears, logs or reacts to `DPI_FIFO_UNDERRUN` (bit 20), `HS_TX_TIMEOUT`
  (bit 21), `LP_RX_TIMEOUT` (22), `TURN_AROUND_ACK_TIMEOUT` (23), `*_FIFO_FULL` or any error bit
  (`grep MIPI_INTR` over `display/` and `i915_irq.c`: only the sites above). The sticky bits 20/21 in
  the dumps are exactly what the code leaves behind.

### 3.2 Is there a MIPI interrupt line into the display IRQ?
Yes: `include/drm/intel/intel_gmd_interrupt_regs.h:11-12` define `I915_MIPIC_INTERRUPT (1 << 19)` and
`I915_MIPIA_INTERRUPT (1 << 18)` in VLV_ISR/IMR/IIR/IER. The driver never uses them:
- `_vlv_display_irq_postinstall()` (`intel_display_irq.c:2091-2106`): `enable_mask` = PORT, PIPE A/B/C
  EVENT, LPE A/B/C, MASTER_ERROR; `VLV_IMR = ~enable_mask` (bit 19 masked), `VLV_IER = enable_mask`
  (bit 19 not enabled); `irq_init()` `intel_display_irq.c:67`.
- With `MIPI_INTR_EN = 0xffffffff` and `HS_TX_TIMEOUT` permanently latched, the MIPI block's interrupt
  output is asserted continuously (VLV_ISR bit 19), but it is masked before IIR and not in IER, so it
  can neither raise a CPU interrupt nor loop the handler (`cherryview_irq_handler` runs
  `do { } while (0)` once per hw interrupt). No interrupt storm is possible from it.
- Consequence: the `0xffffffff` enable is pointless but harmless. A handler for bit 19 would be the way
  to get kernel timestamps of DPI underruns (debug aid): enable `I915_MIPIC_INTERRUPT`, read/clear
  `MIPI_INTR_STAT` in `vlv_display_irq_ack`, log DPI_FIFO_UNDERRUN once per modeset like the pipe
  underrun. Not an upstream-grade fix for anything.

### 3.3 Verdict
Cleared: no missed handling that could cause an interrupt storm or a stall. CONFIRMED gap (not a bug by
the brief's definition): the DSI error/underrun/timeout status is never consumed by the driver on
VLV/CHV, so a DPI FIFO underrun is invisible in the kernel log by construction, while the pipe plane
FIFO underrun is reported. The two must not be confused when reading the log.

---

## 4. Portrait panel / high line rate specifics

- Line time 12.689 us (61000 kHz) or 12.665 us (61111 kHz). Watermark lines: 3 us -> 1 line,
  12 us -> 1 line, 33 us -> 3 lines. method2 always rounds up to at least one full line beyond the
  latency, so no level is under-provisioned: PM2 margin 9.7 us, PM5 margin 0.69 us (12.69 us of
  data vs 12 us latency), DDR DVFS margin 5.07 us (38.07 vs 33).
- PLAUSIBLE (hypothesis, no code error): PM5 is the tightest margin this code can produce (latency
  12 us just below one 12.69 us line). A landscape 1280x720 mode with a 27 us line would have 15 us
  of margin. If the real PM5 wake latency exceeded 12.69 us the result would be a PIPE FIFO underrun,
  which sets PIPEBSTAT bit 31 and (post-modeset) is logged. It was not logged in the covered states,
  so this does not match the evidence (and the DPI underrun is downstream of the plane FIFO anyway).
  Test only if a pipe underrun ever shows up: a test patch would raise PM5 to e.g. 16 us (-> 2 lines)
  or drop the DDR DVFS level (`num_levels = PM5+1`); the latency table is hardcoded.
- `htotal` 774 vs `vtotal` 1308 are used correctly (htotal in the line-time division, `crtc_htotal`
  of `pipe_mode`); no transposition.
- FIFO depth in time is short because of the high line rate (11.4 lines = 144 us single-plane,
  432 us in maxfifo), but the memory latencies covered are an order of magnitude smaller.
- Blanking fraction is low (hblank 54 px = 0.89 us, 7 %; vblank 28 lines = 355 us). method2
  ("large buffer") is the only method VLV/CHV uses; method1 (`i9xx_wm.c:485`) is not used on
  VLV/CHV, so the short hblank does not enter the computation. OK.

---

## 5. Items checked and found OK (so nobody redoes them)
- `vlv_get_fifo_size()` / `vlv_atomic_update_fifo()` DSPARB/DSPARB2 bit positions for pipe B match
  between write and readout; the write path masks DSPARB2 with `0xff` instead of `0x1` for pipes B/C
  (`i9xx_wm.c:1915-1916`, `VLV_FIFO(SPRITEC_HI, 0xff)`), but `DSPARB_SPRITEC_HI_MASK_VLV` is
  `0x1 << 8`, so the `& mask` in the macro limits it to one bit: cosmetic, no effect.
- `DSPFW_CURSORC_WM1_MASK (0x3f << 16)` (`i9xx_wm_regs.h:126`) has the wrong shift (should be `<< 8`)
  but is never used (WM1 registers are zeroed). Cosmetic.
- `vlv_program_watermarks()` ordering: disables DVFS/PM5/cxsr before writing lower watermarks and
  enables them after writing higher ones; `vlv_initial_watermarks` (pre-vblank, intermediate) and
  `vlv_optimize_watermarks` (post-vblank, optimal) are both wired (`vlv_wm_funcs`, `i9xx_wm.c:4110`).
- `chv_set_memory_dvfs()` polls up to 3 ms; a timeout logs `drm_err("timed out waiting for Punit DDR
  DVFS request")`, absent from the boot log.
- Cursor updates do not recompute the FIFO (`dirty & ~BIT(PLANE_CURSOR)`) and do not toggle cxsr;
  cursor show/hide only flips CURSORB/CURSOR_SR between 63 and 0.
- Pixel rate for the DSI pipe is the plain pipe clock (no pfit), `pipe_mode == adjusted_mode`
  (no joiner/splitter on CHV).
- `i9xx_pipestat_irq_ack` is also what clears `PIPE_START_VBLANK_INTERRUPT_STATUS`; bit 2 = 0 in both
  dumps is consistent with the dump being taken between vblanks.
- DSPBCNTR `0x98000400`: ENABLE | format 6 (BGRX888) | TILED; no gamma, no 180 rotation, no async flip.

## 6. Proposed changes
None upstream-grade from this area. Debug aids, optional:
1. Enable pipe underrun reporting on the active GMCH pipe at boot (clear bit 31 first) so the
   fresh-boot state is covered -- or simply do one `output power off/on` after boot before any test
   session so `cpu=yes` holds (cheaper, no patch).
2. A `MIPI_INTR_STAT` logger in `vlv_display_irq_ack` gated by `I915_MIPIC_INTERRUPT` (bit 19 in
   VLV_IER/IMR) to timestamp DPI underruns from the kernel. `MIPI_INTR_EN` must then be reduced to
   the bits of interest (HS_TX_TIMEOUT is permanently latched because of the `u16` bug until fixed),
   and the IRQ enable must follow the display power well like the other VLV bits.
