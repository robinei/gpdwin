# Report D: VLV/CHV DSI enable/disable/modeset sequence, VBT sequences, upstream history

Source audited: `SCRATCH/src/linux-7.2.9/drivers/gpu/drm/i915/display/vlv_dsi.c` (2057 lines),
`intel_dsi_vbt.c`, `intel_dsi.c`, `intel_display.c` (valleyview_crtc_enable / i9xx_crtc_disable /
intel_enable_transcoder), `intel_crtc.c`, `intel_vblank.c`, `intel_display_power_well.c`.
Line numbers below are v7.2.9. Device facts from `data/decode.txt`, `data/glitch-*-regs.txt`,
`data/underrun-log-fresh-boot.log`.

Summary verdicts: no CONFIRMED bug in the enable/disable/modeset sequence itself other than the
already-known `u16` truncation (Fixes tag identified below). Several things cleared; two things worth
testing (host-only re-init experiment; BXT-style DPI FIFO flush bit) are labeled hypothesis/experiment.

---

## 1. Enable / disable sequence audit (vlv_dsi.c)

### 1.1 What the code does on this device (video mode, v3 VBT, port C / pipe B)

Enable (`intel_dsi_pre_enable`, vlv_dsi.c:722-816), runs before pipe/plane enable from
`valleyview_crtc_enable` (intel_display.c:2056, order: `pre_pll_enable` -> `chv_enable_pll` ->
`pre_enable` -> pfit -> color -> watermarks -> `intel_enable_transcoder` -> `vblank_on` -> `enable`):

1. `intel_dsi_wait_panel_power_cycle` (honours `panel_pwr_cycle_delay` = 5000/10 = 500 ms since the
   last POWER_OFF; intel_dsi.c:13).
2. `intel_set_cpu_fifo_underrun_reporting(pipe B, true)` (matches display_info `cpu=yes`).
3. DSI PLL disable+enable (vlv_dsi.c:748-749).
4. `VLV_DSPCLK_GATE_D |= DPOUNIT_CLOCK_GATE_DISABLE` ("can stall pipe", vlv_dsi.c:763).
5. `intel_dsi_prepare` (vlv_dsi.c:1302): MIPI_CTRL(PORT_A) escape divider, MIPI_CTRL(port C) read
   priority, `MIPI_INTR_STAT = 0xffffffff` (clears all latches incl. DPI_FIFO_UNDERRUN),
   `MIPI_INTR_EN = 0xffffffff`, DPHY_PARAM, DPI_RESOLUTION, timings, FUNC_PRG, HS_TX_TIMEOUT
   (truncated), LP_RX/TA/reset timers from VBT, INIT_COUNT (written twice: txclkesc() then the VBT
   value 0x7d0, harmless), EOT_DISABLE=0 (EOT on, clock stop off), HIGH_LOW_SWITCH, LP_BYTECLK,
   DBI_BW, CLK_LANE_SWITCH, VIDEO_MODE_FORMAT = 0x1e. All of these match the good/bad register dumps
   exactly (0x18b80c=0x204, 0x18b820=0x050002d0, 0x18b828..40 = 14/14/14/540/4/10/14,
   0x18b858=0x1e, 0x18b85c=0, 0x18b848=0x42), so the dumps are the driver-programmed state.
6. `MIPI_SEQ_POWER_ON` (VBT: GPIO N 72 high, 20 ms, low, 200 ms, high = panel reset pulse),
   `msleep(panel_on_delay)` = 1000/10 = 100 ms (unconditional, no DEASSERT_RESET sequence exists:
   commit 6fdb335f1c9c), `MIPI_SEQ_DEASSERT_RESET` (absent in this VBT, no-op).
7. `vlv_dsi_device_ready` (vlv_dsi.c:456): rcomp, bandgap reset, ULPS_ENTER, LP_OUTPUT_HOLD on
   **port A ctrl** (common bit, bf344e809011), ULPS_EXIT, DEVICE_READY, 2.5-3 ms sleeps each.
8. `MIPI_SEQ_INIT_OTP` (absent, no-op).
9. `msleep(20)`, `dpi_send_cmd(TURN_ON, LP)` (MIPI_DPI_CONTROL = 0x42, wait SPL_PKT_SENT 100 ms,
   failure only logged), `msleep(100)`.
10. `MIPI_SEQ_DISPLAY_ON` in LP mode: E1 93 / E2 65 / E3 F8 (Jadard/JD936x page unlock), 11 00
    (sleep out), 200 ms, E0 00, 80 03 (lane config), 29 00 (display on), 20 ms. Every packet goes
    through `intel_dsi_host_transfer` (50 ms FIFO-full waits) and then `vlv_dsi_wait_for_fifo_empty`
    (100 ms, drm_err only). Last LP header 0x2905 is still visible in 0x18b86c in the dumps.
11. `intel_dsi_port_enable`: reads port C ctrl (returns 0 on this hw), writes `DPI_ENABLE` (bit 31),
    posting read.
12. `intel_backlight_enable` (PWM) + `MIPI_SEQ_BACKLIGHT_ON` (GPIO N 70 high).
Then the pipe is enabled (`intel_enable_transcoder`). On CHV `intel_crtc_max_vblank_count()` returns
0xffffffff (intel_crtc.c:124), so the driver does **not** wait for the scanline to start moving.

Disable: `intel_dsi_disable` (vlv_dsi.c:830, runs with the pipe still running): BACKLIGHT_OFF (GPIO 70
low, 20 ms), PWM off, `SHUTDOWN` DPI command in LP, 10 ms. Then pipe off, pfit off,
`intel_dsi_post_disable` (vlv_dsi.c:867): wait generic FIFOs empty (DPI FIFO deliberately not in the
mask, a799a9780eb5), clear DPI_ENABLE, 2-5 ms, `intel_dsi_unprepare` (DEVICE_READY 0, reset clocks,
EOT_DISABLE=CLOCKSTOP, clear VID format, DEVICE_READY 1), DISPLAY_OFF (28 00, 17 ms, 10 00, 34 ms),
`vlv_dsi_clear_device_ready` (ULPS enter/exit/enter, AFE_LATCHOUT wait only on port A per
1e08a260b178, LP_OUTPUT_HOLD cleared on port A ctrl, DEVICE_READY 0), PLL off, DPOunit gating
restored, ASSERT_RESET (absent), `msleep(panel_off_delay)` = 50 ms, POWER_OFF (20 ms, GPIO 72 low,
20 ms), `panel_power_off_time` stamp.

### 1.2 Items checked

| # | Location | Finding | Verdict |
|---|----------|---------|---------|
| A | `dpi_send_cmd` vlv_dsi.c:225-253 | SPL_PKT_SENT wait 100 ms; on timeout only `drm_err`, returns 0 regardless. TURN_ON/SHUTDOWN are sent in LP (`DPI_LP_MODE`), SHUTDOWN while HS video is still running (pipe on). This is the documented v3 order (398314516446) and has been so since e1047028e224 (2014). Dumps show SPL_PKT_SENT latched (0x18b804 bit 30) = TURN_ON was acknowledged. No kernel log error on the device. | REJECTED-cleared |
| B | `vlv_dsi_wait_for_fifo_empty` vlv_dsi.c:88 and `intel_dsi_host_transfer` :131 | 100 ms / 50 ms timeouts, failures only logged. Only used for LP init packets at enable and DISPLAY_OFF at disable; dump shows all generic FIFOs empty (0x18b874 = 0x0e060606: LP/HS ctrl+data EMPTY bits set, DPI_FIFO_EMPTY bit 28 clear = video running). The device log has no "DPI FIFOs are not empty"/"Timeout" errors. | REJECTED-cleared |
| C | LP commands during HS video | On this panel no DCS commands are ever sent after DPI_ENABLE: backlight is "PWM from Display Engine" (VBT block 43) plus GPIO 70, no CABC, no TE. The only LP-while-HS transfer is the SHUTDOWN special packet at disable. | REJECTED-cleared |
| D | Port C ctrl register (0x1e1700) reads 0 | `_MIPIC_PORT_CTRL 0x61700` + `VLV_DISPLAY_BASE 0x180000` = 0x1e1700: the driver's address and the measurement agree. The driver itself documents that "the DPI enable bit in port C control register does not get set" on VLV/CHV (vlv_dsi.c:963-971, commits c0beefd29fcb BYT, e6f577893d0a CHV, Cc: stable) and reads `TRANSCONF(PIPE_B)` instead for hw-state readout. Port A ctrl 0x1e1190 = 0x00030000 = `LP_OUTPUT_HOLD` (bit 16, set by `vlv_dsi_device_ready` on PORT_A as the common bit) | `AFE_LATCHOUT` (bit 17, PHY in HS). Consistent with the driver. 0x1e1708/0x1e170c are not defined in `vlv_dsi_regs.h` (only 0x61704 TEARING_CTRL); bit 31 toggling there is unexplained but the driver never touches them. | REJECTED-cleared (hardware quirk, known upstream) |
| E | `intel_dsi_port_enable` writes `temp | DPI_ENABLE` where `temp` was read back as 0 | Single link, RGB888: all other fields are legitimately 0 (no dual link, no dithering). Fine. | REJECTED-cleared |
| F | `vlv_dsi_clear_device_ready` only checks AFE_LATCHOUT/LP_OUTPUT_HOLD on port A for a port C panel | Intentional (1e08a260b178, bf344e809011): those bits exist only in the port A register and are common to both ports. | REJECTED-cleared |
| G | Ordering DISPLAY_ON (sleep out/display on) before DPI_ENABLE | The in-file comment table (vlv_dsi.c:692-701) lists "turn on DPI" before "MIPIDisplayOn" for v3, and the code sends the TURN_ON *packet* before DISPLAY_ON but sets DPI_ENABLE after it. This order has been identical since the first video-mode code (2634fd7fd851, 2014: TURN_ON, 100 ms, panel enable, DPI_ENABLE) and is the order used by essentially every BYT/CHT tablet. Not demonstrably wrong; the GOP/firmware order is unknown. | hypothesis only, not a bug |
| H | Delays | VBT v3 delays are inside the sequences (25b4620ee822); driver adds panel_on 100 ms, 20+100 ms around TURN_ON, panel_off 50 ms, power cycle 500 ms. All equal or longer than the VBT PPS values. Nothing too short. | REJECTED-cleared |
| I | `intel_dsi_prepare` sets `MIPI_INTR_EN = 0xffffffff` (vlv_dsi.c:1349, "XXX: why here, why like this? handling in irq handler?!") | No code anywhere in 7.2.9 reads MIPI_INTR_STAT except `dpi_send_cmd`/`intel_dsi_host_transfer` (grep over drivers/gpu/drm/i915: no handler, no MIPI bit in the VLV IER/IIR definitions). DPI_FIFO_UNDERRUN (bit 20) and HS_TX_TIMEOUT (bit 21) are latched and never cleared at runtime; they are cleared only by the next `intel_dsi_prepare` (next modeset). So the driver has no knowledge of DSI-side underruns and nothing to react to them. Not a bug in itself (IRQ agent owns whether the line is routed). | cleared / informational |
| J | VBT fields parsed but never programmed on VLV/CHV: `hs_tx_timeout` (0x3fffff), `blanking_pkt` ("blanking packets during BLLP"), `lp_clock_during_lpm` (intel_dsi_vbt.c:775-789, only printed by `intel_dsi_log_params`) | The GOP evidently programs `hs_tx_timeout` from the VBT (fresh boot register = 0x3fffff = VBT HSTxTimeOut), the driver computes its own (and truncates it). ICL+ programs the VBT value (5a4712f472bf). Using the VBT value on VLV/CHV would make the driver match the firmware exactly; a clean alternative to fixing the arithmetic. The other two fields have no VLV/CHV register in the driver headers. | informational, feeds the HS_TX_TIMEOUT patch |
| K | Driver-computed D-PHY/switch values differ from the VBT register values: driver DPHY_PARAM 0x1c0d300a (dump 0x18b880) vs VBT "Dphy Params" exit 0x3f/trail 0x1f/clkzero 0x7f/prep 0xf; HIGH_LOW_SWITCH_COUNT 0x18 (dump 0x18b844) vs VBT 0x46; CLK_LANE_SWITCH 0x001b000c (dump 0x18b888) vs VBT 0xa0014; LP_BYTECLK 3 vs VBT 4 | Driver derives these from the D-PHY timing table (`vlv_dphy_param_init`), ignoring the VBT's ready-made register values. Arithmetic is another agent's area; I note only that the desync was also observed in the fresh-boot (GOP-programmed) state, so neither value set prevents it. We do not have a fresh-boot dump of 0x18b800-0x18b8ff; capturing one (read-only) would show the exact GOP programming for comparison. | hypothesis / data gap |
| L | `intel_dsi_pre_enable` enables backlight (PWM + GPIO 70) before the pipe is enabled | Cosmetic (a few ms of unlit/garbage frame), not a stability issue. | cleared |

### 1.3 What happens on a DPI FIFO underrun, and recovery paths

- In non-burst-with-sync-events mode the DSI line in byte clocks (540 + 14 + 14 (+14)) is consumed at
  exactly the pipe's pixel rate (pixel clock : byte clock = 61111 : 45833 = 4 : 3; 720 px = 540
  byte clocks = 11.78 us). There is no slack; the DPI FIFO is the only buffer between the pipe and
  the serializer.
- The pipe-level underrun (PIPEBSTAT bit 31 `PIPE_FIFO_UNDERRUN_STATUS`) is **not** set in the bad
  dump (0x10040603), and the kernel log is silent, so the plane FIFO did not run dry; the stall is
  between the pipe output and the DSI serializer (DPI FIFO). Note the driver's own comments that the
  DPO unit can stall the pipe (vlv_dsi.c:762 "Disable DPOunit clock gating, can stall pipe";
  intel_display_power_well.c:1267 "avoid the pipe getting stuck (and never recovering)"): the pipe is
  subject to back-pressure from the DSI side, it is not a free-running source.
- **Upstream precedent for exactly this symptom:** commit f90e8c36c886 ("drm/i915/dsi: fix bxt split
  screen and color issue", 2016): "the display appears split, or shifted about 2/3 of the screen, and
  the color components are cycled ... missing the crucial BXT_DEFEATURE_DPI_FIFO_CTR bit in the
  EOT_DISABLE register. Per bspec, with the bit set, the mipi_dpf_vblank_start signal is asserted only
  when the complete frame is transferred in the DPHY line and also the DPI FIFO is flushed out at the
  end of each frame." The mechanism (DPI FIFO residue carried across the frame boundary -> persistent
  shift and RGB component rotation) matches the GPD symptom (bottom half on top, red/blue swapped,
  persists until the port is disabled). On BXT the panel fitter mitigated it; the driver sets bits 8/9
  of MIPI_EOT_DISABLE on BXT/GLK only (vlv_dsi.c:1375-1379). Whether CHV's MIPI block has an
  equivalent bit is unknown (CHV `0x18b85c` reads 0; bits 8/9 are undocumented in vlv_dsi_regs.h for
  VLV/CHV). **Experiment (hypothesis, low risk, patched module + reboot, not a live poke):** set
  `BXT_DEFEATURE_DPI_FIFO_CTR` (bit 9) also on CHV in `intel_dsi_prepare` and see whether the register
  reads back 1 and whether the desync frequency changes. If the bit does not stick the experiment is
  null.
- In the driver nothing resynchronises the link short of a full encoder disable/enable. The
  "disable" half alone (`DPI_ENABLE` clear, 2-5 ms, re-enable) is the smallest hardware operation
  that flushes the DPI FIFO; there is no path that does only that.

## 2. Self-recovery / fastset / DPMS

- No periodic or self-recovery path exists: `encoder->update_pipe = intel_backlight_update`
  (vlv_dsi.c:1945) is the only fastset hook and touches just the backlight; `encoder->enable` is NULL
  on VLV/CHV (bxt_dsi_enable only on BXT/GLK); there is no DSI interrupt handler (see I).
- Boot: `vlv_dsi_init` adopts the GOP pixel clock when it fuzzily matches the VBT clock
  (vlv_dsi.c:1998-2010, "Using GOP pclk"; display_info shows `pipe__mode` 61111 vs mode 61000) so the
  initial commit is a fastset/no-op. This is why a fresh boot keeps the firmware's MIPI programming
  (HS_TX_TIMEOUT 0x3fffff) until the first real modeset. Consequence: the fresh-boot desync happened
  with the **GOP's** full register set (timings, D-PHY, timeouts), the later ones with the driver's.
  Both sets desync, so the register values the driver computes differently from the GOP (K) are not
  the single cause either.
- sway `output DSI-1 power off/on` (wlroots atomic: CRTC `ACTIVE`=0 then 1; legacy DPMS maps to the
  same) is `drm_atomic_crtc_needs_modeset()` -> full `i9xx_crtc_disable` + `valleyview_crtc_enable`,
  i.e. the complete sequence of 1.1 including SHUTDOWN, DISPLAY_OFF (sleep in), ULPS, PLL off, panel
  POWER_OFF (GPIO 72 low), 500 ms power-cycle delay, POWER_ON reset pulse, sleep out, display on.
  So the known fix resets **both** the host DSI block and the panel; it does not tell which side is
  desynced.
- **Experiment to split host vs panel (hypothesis, patched module):** a module parameter that makes
  the disable/enable skip POWER_OFF/POWER_ON, DISPLAY_OFF/DISPLAY_ON and the backlight GPIO (keeping
  SHUTDOWN/TURN_ON, DPI_ENABLE, DEVICE_READY/ULPS, PLL). If a power off/on with that parameter still
  fixes a desynced picture, the fault is host-side (DPI FIFO / serializer) and the BXT-style flush is
  the right direction; if it does not, the panel's controller is the one losing frame alignment.

## 3. Upstream history

### 3.1 v7.2.9..master (torvalds, v7.3-rc6+) for vlv_dsi*.{c,h}, intel_dsi*.{c,h}
Only two commits, both refactors (`78e6cdf518f5` intel_panel_compute_config calling convention,
`3729f93c197e` pass atomic state to .compute_config). No DSI fix anywhere in drivers/gpu/drm/i915
since v7.2.9 (grep of subjects for dsi/mipi/chv/cherryview: nothing). No stable fix is pending.
`origin/HEAD:vlv_dsi.c` still has `static u16 txbyteclkhs(u16 pixels, ...)` (line 56) and the
`crtc_vtotal * crtc_htotal` call (line 1408).

### 3.2 Fixes tag for the u16 truncation
`txbyteclkhs()` was introduced as `static u16 txbyteclkhs(u16 pixels, int bpp, int lane_count)` and
`MIPI_HS_TX_TIMEOUT` was computed from `vtotal * htotal` in the very first DSI commit; the signature was
later only extended (burst_mode_ratio in 7f0c860533ff, 2014; `pixels_from_txbyteclkhs` added for BXT
readout in cefc4e187851 / 130b62f74af3, 2016, same u16). `git log -G'txbyteclkhs\(u(16|32)'` over the
whole tree shows nobody ever changed the width; no revert or attempted fix exists.

    Fixes: 4e646495c615 ("drm/i915: add basic MIPI DSI output support")

(12-char sha 4e646495c615, Jani Nikula, 2013-08-27, v3.13.) `pixels_from_txbyteclkhs()` (BXT-only
readout) could carry `Fixes: cefc4e187851 ("drm/i915/BXT: Retrieving the horizontal timing for DSI")`
if fixed in the same or a second patch. The ICL counterpart (5a4712f472bf, 2018) programs the VBT
`hs_tx_timeout` directly, which is what the GOP does on this device (fresh boot 0x3fffff).

### 3.3 History commits relevant to the symptom (full log back to 2013, 574 commits triaged)
- f90e8c36c886 2016 "drm/i915/dsi: fix bxt split screen and color issue" - same visual symptom, DPI FIFO not flushed at frame end; BXT-only bit (see 1.3).
- 20e5bf667aa7 2014 "Disable DPOunit clock gating" - "can stall pipe"; shows DSI side can stall pipe B. Still in place (vlv_dsi.c:763, power well preserves it).
- c315faf8e6ec 2014 "dsi: fix pipe-off timeout due to port vs. pipe disable ordering" - pipe must be disabled before DPI; origin of SHUTDOWN-while-pipe-runs order.
- a799a9780eb5 2014 "DPI FIFO empty check is not needed" - DPI FIFO excluded from the empty wait (it is never empty while the pipe runs).
- 2634fd7fd851 2014 "Enable MIPI port before the plane and pipe enable" - origin of DPI_ENABLE in pre_enable (hw team recommendation).
- e1047028e224 2014 "Send DPI command explicitely in LP mode" - TURN_ON/SHUTDOWN in LP.
- 24d9c40140ff 2014 "Enable RANDOM resolution support" - RANDOM_DPI_DISPLAY_RESOLUTION always set (bit 4 of 0x18b858 = present in dump).
- c0beefd29fcb 2014 / e6f577893d0a 2016 (Cc: stable) - port C DPI_ENABLE never reads back on BYT/CHV; readout uses pipe B. Explains 0x1e1700 == 0.
- bf344e809011 2014 "Enable MIPI PHY transparent latch for DSI Port C" / 1e08a260b178 2017 "Only wait for LP00 on MIPI PORT A" - port A ctrl holds the common bits (explains 0x1e1190 = 0x30000).
- 20dbe1a1cbf3 2015 "Changes required to enable DSI Video Mode on CHT" - CHV DSI PLL m/n/p (PLL agent).
- cd2d34d9b61f 2016 "Setup DPLL/DPLLMD for DSI too on VLV/CHV", ae9ec62bdadc "Fix CHV DSI PLL refclk during state readout" (PLL agent).
- 25b4620ee822 2017 "Skip delays for v3 VBTs in vid-mode" + 6fdb335f1c9c 2020 (panel_on_delay unconditional when no DEASSERT_RESET: applies here).
- 398314516446 2017 "Document always using v3 SHUTDOWN / MIPI_SEQ_DISPLAY_OFF order".
- 1d5c65edd930 2016 / f13c2a0032f0 2020 / c87eba80470e 2021 - panel power cycle delay handling (500 ms here).
- 33c8d8870c67 2017 Revert "drm/i915/bxt: Disable device ready before shutdown command" - the only revert in these files; BXT-only, not applicable.
- fceeca7f3cf1 2022 "vlv: fix pixel overlap register update" - dual-link only, n/a.
- vlv_dsi_min_cdclk (vlv_dsi.c:1756): "On Valleyview some DSI panels lose (v|h)sync when the clock is lower than 320000KHz" - VLV-only floor; CHV returns 0 and relies on the generic cdclk computation (watermark/clock agents: check what cdclk the CHV actually runs at under this 61 MHz mode; a DSI sync-loss-vs-cdclk precedent exists on the sister platform).
- i9xx_wm.c (only subjects, other agent's area): 1a10ae6ba8c3 2017 "Workaround VLV/CHV sprite1->sprite0 enable underrun" (sprites unused here), 290248c27c93 2019 "Implement new w/a for underruns with wm1+ disabled" (intel_pm.c, VLV/CHV wm path), 5b9489cb8ee2 2017 "Calculate vlv/chv intermediate watermarks correctly, v3", 262cd2e154c2 / 54f1b6e15db8 2015 CHV DDR DVFS + dynamic FIFO split, 58590c14d80d 2015 "Don't try to use DDR DVFS on CHV when disabled in the BIOS".

## 4. Prior-art patch (ViccRondo/gpd-win1-atomic-gaming, fetched to SCRATCH/work-D/vicc/)
`kernel/patches/0001-drm-i915-chv-retry-stalled-dsi-transcoder-enable.patch` (against Fedora
6.19.10). Changes: `intel_wait_for_pipe_scanline_moving()` returns bool; in `valleyview_crtc_enable`,
after `intel_enable_transcoder()` and before `intel_crtc_vblank_on()`, on CHV+DSI it waits for PIPEDSL
to move (100 ms) and, if it does not, logs a warning, does `intel_disable_transcoder` / 1-2 ms /
`intel_enable_transcoder` once and waits again. The DSI port (DPI_ENABLE, device ready) is left as is;
only TRANSCONF is toggled with planes still off. Their symptom: after suspend/resume the transcoder
reports enabled but the scanline counter never starts, and the next commit dies with `flip_done timed
out` (same message Robin saw after the live register write). Their own doc says "not yet a proven
fix". Assessment: it does not identify a bug in the enable sequence; it is a blind retry. What it does
show is (a) stock CHV never verifies that pipe B started after a DSI modeset (max_vblank_count !=
0, intel_display.c:496), and (b) a CHV DSI pipe can sit enabled-but-stalled, which corroborates the
back-pressure picture in 1.3. Not applicable to the glitch (the pipe is running in the bad state;
vblanks and flips work). Worth keeping in mind only if resume stalls ever appear on the GPD.

## 5. Things not done / data gaps
- No fresh-boot (GOP-programmed) dump of 0x18b800-0x18b8ff and 0x1e1190/0x1e1700 exists; one
  read-only capture before the first modeset would show the firmware's D-PHY/switch/timing values
  for comparison with the driver's (item K).
- The CHV MIPI PRM was not available; whether `MIPI_EOT_DISABLE` bit 9 exists on CHV is unknown.
