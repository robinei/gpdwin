# Findings: i915 DSI on CHV (GPD Win 1), audit of v7.2.9 (2026-10-09)

Scope: `TASK.md`. Code audited: `drivers/gpu/drm/i915/display/` `vlv_dsi.c`, `vlv_dsi_pll.c`,
`intel_dsi_vbt.c`, `vlv_dsi_regs.h`, `i9xx_wm.c` (VLV/CHV path), `intel_fifo_underrun.c`,
`intel_display_irq.c`, `intel_cdclk.c` (VLV/CHV), plus upstream history to v7.3-rc6. Line numbers are
v7.2.9. Detailed reports: `reports/B-clocks.md`, `reports/C-watermarks.md`,
`reports/D-sequence-history.md` (the register audit A is summarised here). Arithmetic
reimplementations: `scripts/` (copied from the analysis, paths inside point at a scratch dir).

**Nothing here is known to cause the glitch.** One confirmed bug (does not cause it, refuted earlier),
two experiments modeled on upstream fixes for the same symptom on sibling hardware, the rest cleared.

## Panel numbers used everywhere
720x1280, h 720/738/756/774, v 1280/1294/1298/1308, VBT clock 61000 kHz. The driver adopts the GOP's
pclk 61111 kHz (`vlv_dsi_init`, fuzzy clock check): DSI PLL M=88 N=4 P=6 from the 100 MHz CHV ref gives
366.667 Mbit/s per lane, byte clock 45.833 MHz, pixel clock 61.111 MHz, refresh **60.36 Hz** (not
60.25). 4 lanes, RGB888, non-burst with sync events, EOT on, clock stop off. Pipe line = 774 px =
580.5 byte clocks (12.665 us); frame = 759,294 byte clocks (16.57 ms). cdclk **266667 kHz** (measured,
both at fresh boot from the GOP and after driver modesets; max on CHV is 320000).

## Measured state (new this session)
`data/fresh-boot-regs-2026-10-09.txt`: a fresh boot (no driver modeset) has **exactly the driver's
values** in every MIPI port C register except `MIPI_HS_TX_TIMEOUT` (0x3fffff vs 0x55ff) and
`MIPI_INTR_STAT` (0 vs 0x40200000: bit 21 HS_TX_TIMEOUT and bit 30 appear only after a driver
modeset). So the GOP and the driver program the same link; the desync happens with both. The GOP does
not set `EOT_DISABLE` bit 9 on port C; port A `EOT_DISABLE` = 0x100 (bit 8), not written by the driver.
Pipe B FIFO underrun reporting is on at boot (`cpu=yes`).

## CONFIRMED bug

### 1. `MIPI_HS_TX_TIMEOUT` truncated to 16 bits (patch 0001)
- `vlv_dsi.c:56-69` `txbyteclkhs()`/`pixels_from_txbyteclkhs()` take and return `u16`. `vlv_dsi.c:1407`
  (non-burst) passes `crtc_vtotal * crtc_htotal` = 1,012,392 -> 29,352 -> register 22,015 = **0x55ff**
  (measured) instead of **0xb95ff** (759,295 = one frame + 1, as the comment at 1384-1398 asks). 0x55ff
  is 0.48 ms = 38 lines. Both the argument and the return type must widen (only the argument: 0x95ff).
  Also, `pixels * bpp * 100` overflows 32 bits for frames above ~1.79 M px, so the patch multiplies in
  64 bits.
- Since `4e646495c615` ("drm/i915: add basic MIPI DSI output support", 2013); nobody has touched the
  width since; master (v7.3-rc6+) still has it. No stable fix pending.
- Effect: only this register changes on this panel. Not the desync cause (desync seen with 0x3fffff).
  Fit to send upstream: `kernel/i915/0001-*.patch` (needs Robin's Signed-off-by; draft, not sent).

## Experiments (PLAUSIBLE, untested; each is one patch, test one at a time)

### 2. cdclk 266667 kHz with DSI on CHV (patch 0002) — tested, no improvement
- `vlv_dsi.c:1756-1778` `vlv_dsi_min_cdclk()` returns 320000 for Valleyview only, CHV gets 0.
  `vlv_calc_cdclk()` (`intel_cdclk.c:569`) then picks 266667 for this pixel rate (min_cdclk ~64 MHz).
- Upstream `c8dae55a8ced` ("drm/i915/vlv: Add cdclk workaround for DSI", Hans de Goede 2017, acked by
  Ville Syrjälä): at 266667 kHz a Bay Trail DSI panel showed the image **shifted ~20 % with
  wraparound and sometimes wrong colors**, "the panel controller starting to sample the datastream
  somewhere mid-line"; fixed by requiring >= 320000. That is our symptom (split picture, red/blue
  swapped). The DSI block is the same IP on CHV; the CHV case was never evaluated.
- Not a provable bug from code (no spec says CHV needs it), hence experiment. Patch 0002 extends the
  minimum to CHV; 320000 is CHV's max cdclk, so it is always reachable. Side effects: PFI credits 63
  instead of 12 (`vlv_program_pfi_credits`), higher display voltage level; pixel rate, DSI PLL and
  watermarks unchanged. By the code, every panel reset already cycles cdclk 266667 -> 200000 -> 266667
  (all pipes off), so cdclk changes themselves are routine.
- Fits the data: GOP also runs 266667 (fresh-boot desync explained as well as later ones).
- **Test 2026-10-09:** with 0002 at cdclk 320000 the DPI underrun latch and a momentary flash still came
  34 s into DevilutionX. So cdclk does not stop the underruns. Whether persistent desyncs stop is still
  being watched (results: docs/hardware.md "i915 patch tests")
  At ~16:35 a split happened with cdclk verified at 320000 (held several seconds, then recovered by itself,
  which stock splits also sometimes did). **Verdict: no improvement seen; cdclk is not the cause.**
  Register snapshot during a split with 0002 (`data/split-0002-regs.txt`): same as the stock bad state, only the
  DPI underrun latch differs from good; pipe B no FIFO underrun.

### 3. DPI FIFO flush at end of frame (`BXT_DEFEATURE_DPI_FIFO_CTR`) — bit does not exist on CHV
- Upstream `f90e8c36c886` ("drm/i915/dsi: fix bxt split screen and color issue", 2016): "display
  appears split, or shifted about 2/3 of the screen, and the color components are cycled", fixed by
  `EOT_DISABLE` bit 9; per bspec with it set, vblank start is signalled only when the frame is fully
  transferred and the DPI FIFO is flushed at the end of each frame. Set for BXT/GLK only
  (`vlv_dsi.c:1375-1379`).
- Whether bit 9 exists on CHV is unknown (no CHV documentation found); the GOP leaves it 0 on port C,
  but bit 8 is set on port A (0x100), so the defeature bits may exist. Writing an undocumented bit is
  riskier than experiment 2; do it only if 2 changes nothing.
- **Tests 2026-10-09 (patches 0003, 0004):** bit 9 written to port C `EOT_DISABLE` reads back 0; set by
  read-modify-write in port A's `EOT_DISABLE` (holds bit 8 = 0x100 from the GOP) it is dropped too (still
  0x100). CHV does not implement the bit in either register: this route is closed.
- Mechanism supported by observation: one split was a few-pixel shift with rotated colors, i.e. the stream
  offset by a byte count that is not a multiple of 3 (RGB888). The pixel stream loses byte alignment and keeps
  it frame after frame; nothing re-aligns at frame start.

### 4. Other hypotheses, no patch
- HSYNC_PADDING programmed (14) in sync-events mode where the code comment says it is ignored; if the
  hardware used it, the link line would be 582 byte clocks vs 580.5 pipe (overflow, not underrun). The
  GOP programs the same value. Not actionable.
- h-counts rounded up (13.5 -> 14) where gma500 Medfield (same IP) rounded down; semantics undocumented;
  GOP does the same.
- `vlv_dsi.c:1719`: `hs_to_lp_switch` adds `ths_trail` in ns (60) to `tlpx` in UI (19); should be UI
  (22). Gives 13 instead of 8, masked by `max(21, ...)`: no effect on this panel. Real unit bug
  (cosmetic here), could ride with 0001 upstream.
- `struct intel_dsi` timeout fields (`intel_dsi.h:104-112`) are u16 for 24-bit registers; values fit on
  this VBT. `hs_tx_timeout` (0x3fffff in VBT) truncates to 0xffff but is used only by ICL
  (`icl_dsi.c`). Latent, not CHV.
- Panel PM5 watermark margin: 1 line (12.69 us) vs 12 us latency, tightest the code allows; a PM5 miss
  would be a logged pipe underrun, none seen.

## Cleared (checked, no bug)
- Every MIPI port C register in the dumps equals the driver's arithmetic to the bit (DSI_FUNC_PRG 0x204,
  DPI_RESOLUTION, HACTIVE 540, HFP/HSYNC/HBP 14, V 14/4/10, VIDEO_MODE_FORMAT 0x1e, DPHY_PARAM 0x1c0d300a,
  HIGH_LOW_SWITCH 24, CLK_LANE_SWITCH 0x001b000c, LP_BYTECLK 3, INIT_COUNT 2000, LP_RX/TA/RESET from VBT).
  Register base 0x180000 and `_MIPI_PORT` offsets agree with the hardware.
- **0x1e1700 reading 0** is a documented hardware quirk: DPI_ENABLE never reads back on VLV/CHV port C
  (`vlv_dsi.c:963-971`, `c0beefd29fcb`, `e6f577893d0a`); the driver checks TRANSCONF B instead.
  0x1e1190 = 0x00030000 = LP_OUTPUT_HOLD | AFE_LATCHOUT on port A, set on purpose (common bits).
  0x1e1708/0x1e170c are not defined in i915 or gma500; never written; look like live status words.
- DSI PLL (`vlv_dsi_pll.c`): no bug; dividers, ranges, lfsr indices, CHV ref, readout, lock wait fine.
  Pixel and byte clock come from one VCO with a fixed ratio: no pipe/link rate drift. Link line needs
  ~571.5 of 580.5 byte clocks (HSYNC ignored): 9 clocks slack. A rate error would fail every line, not
  every 1.5-2 min.
- Watermarks/FIFO (CHV): arithmetic correct for this panel (FIFO 511 lines to plane B; PM2/PM5/DDR DVFS
  45/45/135 cachelines; DSPFW1 0xbc3f7800 and DDL 0x82828282 measured as computed; DSPHOWM measured
  0x02201001 vs 0x02001000 predicted by the reimplementation, not followed up).
  DDL fixed by design, trickle feed off via MI_ARB.
- Pipe FIFO underrun detection: PIPESTAT bit 31 is polled on every display interrupt and after every
  commit, reporting is on for pipe B in every state seen (also at fresh boot). "No `CPU pipe B FIFO
  underrun` message" is valid evidence: **the plane FIFO did not underrun; only the DSI DPI FIFO did**.
  The problem is between the pipe output and the DSI serializer, not memory fetch.
- `MIPI_INTR_EN` = 0xffffffff with no handler: the CPU interrupt (VLV_IIR bit 19) is masked in IMR and
  absent from IER; no storm. The latched DPI_FIFO_UNDERRUN / HS_TX_TIMEOUT bits are only cleared by the
  next `intel_dsi_prepare`. No driver code ever looks at them.
- Enable/disable sequence: timeouts only log on failure (no such messages); no LP commands during HS
  video on this panel; DISPLAY_ON before DPI_ENABLE unchanged since 2014; no self-recovery path. sway
  `output power off/on` is a full CRTC disable/enable incl. panel reset (GPIO 72) and cdclk cycle, so
  the manual fix resets host and panel together (does not tell which side lost sync).
- Boot is a fastset: the GOP's link programming stays until the first modeset.
- Prior art `drm/i915/chv: Retry stalled DSI transcoder enable` (ViccRondo): toggles TRANSCONF if PIPEDSL
  does not move after enable (resume stall). Blind retry, no sequence bug shown, different symptom.
- Upstream v7.2.9..master: only two refactors in these files; no fix to backport.

## Recipe and test
`kernel/i915/`: `build.sh` (desktop; pinned kernel.org tarball, Arch patch, Arch linux-headers in
`sources.sha256`; `./build.sh 0002` builds with patch 0002 only), `install-test.sh` (GPD: separate
initramfs + one-shot boot entry, stock stays default), `uninstall-test.sh`. Test plan and results:
`docs/hardware.md` "Display", section "i915 patch tests".
