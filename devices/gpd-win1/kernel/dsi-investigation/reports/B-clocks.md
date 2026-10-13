# Report B: clocks and rate matching, pipe B <-> DSI port C (i915 v7.2.9, CHV)

Source: `drivers/gpu/drm/i915/display/` of pristine v7.2.9. Arithmetic reproduced in
`SCRATCH/work-B/dsi_clocks.py` (C integer semantics, run with `python3 -I`).
Panel: 720x1280, htotal 774, vtotal 1308, VBT clock 61000 kHz, RGB888, 4 lanes, non-burst sync events,
burst_mode_ratio 100, EOT on, clock stop off.

## 1. DSI PLL and pixel clock: what the driver computes (CONFIRMED arithmetic, no bug)

Call chain: `intel_dsi_vbt_init()` (intel_dsi_vbt.c:799) sets `intel_dsi->pclk = mode->clock = 61000`
(non-burst, not dual link -> no adjustment, `burst_mode_ratio = 100`, line 847). `vlv_dsi_init()`
(vlv_dsi.c:1999-2007) then replaces pclk by the hardware read-back if within 5 % (`intel_fuzzy_clock_check`):
the GOP-programmed PLL reads back as 61111 (see below), so `intel_dsi->pclk` is most likely 61111 at runtime.
Both inputs give the same PLL result:

| step | code | value |
|---|---|---|
| target DSI clock | `dsi_clk_from_pclk` (vlv_dsi_pll.c:57) = ROUND(pclk*24/4) | 366000 (pclk 61000) or 366666 (pclk 61111) kHz |
| search | `dsi_calc_mnp` (:62-116): CHV ref 100000 kHz, N=4, M 70..96, P 2..6, `calc = (m*ref)/(p*n)` truncating | best M=88, P=6: 8800000/24 = 366666 (delta 666 resp. 0) |
| registers | `ctrl = 1 << (17 + P - 2)` = 0x00200000; `div = (ffs(4)-1)<<16 \| lfsr_converts[88-62]` = 0x00020038 (m_seed 56) | plus CLK_GATE_DSI1_DSIPLL (bit 7) and VCO_EN (bit 31) -> ctrl 0x80200080 |
| exact bit clock per lane | 88 * 100 MHz / (6*4) | **366.6667 MHz** (UI 2.727 ns) |
| byte clock (txbyteclkhs) | /8 | **45.8333 MHz** |
| pixel clock | bitclk * 4 / 24 | **61.1111 MHz**; `vlv_dsi_pclk` (:166-168): (88*100000)/(6*4) = 366666 -> ROUND(366666*4/24) = **61111** |
| refresh | 61111.11 kHz / (774*1308) | **60.363 Hz** (not 60.253; that figure is the VBT's 61000 kHz) |

- The read-back 61111 kHz (`pipe__mode`, `port_clock=61111`) is reproduced exactly; `vlv_dsi_get_pclk` and
  `vlv_dsi_pclk` decode the same registers the compute path writes (P from one-hot bit via `fls`, M via
  lfsr table lookup, N via log2 field). Round-trip is lossless for this panel.
- Pixel clock is 0.18 % above the VBT's 61000 kHz. That is the granularity of the PLL (next candidates
  M=73/P=5 = 365000 kHz -> 60833 kHz, M=74/P=5 = 370000 -> 61666). The panel sees 61.111 MHz either way
  (GOP and driver pick the same divisors, which is why the fuzzy check adopts the GOP pclk).
- **Pipe clock source (assumption, no public register doc):** on VLV/CHV with DSI the pipe DPLL is not
  used (`chv_dpll()` intel_dpll.c:1438-1440 leaves VCO off, only ref/CRI clock bits set; `chv_crtc_clock_get`
  :555-557 skips readout). The pipe pixel clock is derived from the DSI PLL inside the DSI/CCK block; the
  driver assumes pixclk = bitclk * lanes / bpp (that is what `vlv_dsi_pclk` reports as crtc_clock; the
  readout is derived from the same formula, so the state checker cannot catch a hardware disagreement).
  Consequence: pixel clock and byte clock come from one VCO with a fixed 4:3 ratio. There is no drift
  between the pipe and the link; any phase relation is constant from line to line.

**Verdict: REJECTED-cleared** for "pixel clock vs link rate mismatch". Long-term rates are identical by
construction; a static mismatch would underrun or overflow deterministically on every line, not every
1.5-2 minutes.

## 2. Line budget in byte clocks (set_dsi_timings, vlv_dsi.c:1214-1283)

`txbyteclkhs(px) = DIV_ROUND_UP(DIV_ROUND_UP(px*24*100, 800), 4)` = ceil(ceil(3*px)/4):

| register (port C, dump) | code | value | bytes on 4 lanes |
|---|---|---|---|
| HACTIVE_AREA_COUNT 0x18b834 | txbyteclkhs(720) | 540 (0x21c) matches dump | 2160 = exactly 720 px * 3 B, no header/CRC |
| HFP_COUNT 0x18b830 | txbyteclkhs(738-720 = 18) | 14 (0xe) matches | 56 (18 px would be 54 B = 13.5 clk, rounded up) |
| HSYNC_PADDING_COUNT 0x18b828 | txbyteclkhs(756-738 = 18) | 14 (0xe) matches | 56 |
| HBP_COUNT 0x18b82c | txbyteclkhs(774-756 = 18) | 14 (0xe) matches | 56 |
| VFP/VSYNC/VBP 0x18b840/38/3c | lines | 14 / 4 / 10 match | |
| DPI_RESOLUTION 0x18b820 | 1280<<16 \| 720 | 0x050002d0 matches | |

(Note: the task text listed the three 0xe registers as "HFP/HSYNC/HBP at 0x18b828..0x18b830"; per
`vlv_dsi_regs.h` 0xb828 is HSYNC_PADDING, 0xb82c HBP, 0xb830 HFP, 0xb834 HACTIVE. All three are 14 so the
mix-up is harmless. The driver's `_MIPI_PORT` offsets (0xb800 block for port C relative to
`VLV_DISPLAY_BASE` 0x180000) agree with the dump: every register value decodes consistently.)

Timing, per line, exact fractions:

- Pipe line: 774 px / 61.1111 MHz = **12.6655 us = 580.5 byte clocks = 2322 bytes** of link capacity.
- Pipe active: 720 px = 540 byte clocks (= HACTIVE count, exact).
- Pipe blanking: 54 px = 40.5 byte clocks = 162 bytes available for everything that is not RGB payload.
- Programmed blanking counts: HFP+HSYNC+HBP = 42 byte clocks = 168 bytes (> 162). Sum of all four
  counts = 582 = pipe line + 1.5 byte clocks (+32.7 ns).
- What the controller must put on the wire per line in non-burst sync-events mode (MIPI DSI spec
  8.11.2): HSS short packet 4 B, RGB long packet header 4 B + CRC 2 B, EOT 4 B (EOT enabled,
  0x18b85c = 0), plus HBP and HFP blanking (long packets carrying `hbp`/`hfp` bytes, or LP-11 if the
  controller goes to low power during blanking). Fixed overhead = 14 B = 3.5 byte clocks.
  - If HSYNC_PADDING is ignored in sync-events mode (the driver's register comment at vlv_dsi.c:1271 and
    the register header say "meaningful for sync pulse mode only, can be zero for sync events"):
    link line = 1 (HSS) + 14 (HBP) + 1.5 (hdr+crc) + 540 + 14 (HFP) + 1 (EOT) = **571.5 byte clocks**,
    i.e. **9 byte clocks (36 B, 196 ns) of slack** per 12.67 us line. Link slightly faster than the pipe,
    the controller must wait for the pipe (idle/blanking) each line. Consistent with a working display.
  - If HSYNC_PADDING were also transmitted: 585.5 > 580.5, the link would be 5 byte clocks/line slower
    than the pipe and the DPI FIFO would overflow after a handful of lines. The display works for minutes,
    so this is not what the hardware does.
- Per frame (1308 lines): pipe 16.566 ms; link needs at most 1308 * 571.5 = 747522 byte clocks = 16.309 ms
  plus vertical sync/blanking short packets, within budget.

Rounding direction of `txbyteclkhs` is up (so blanking packets are never shorter than the pipe's
blanking), which is the safe direction for an underrun but eats slack: with the exact values (13.5) the
slack would be 10.5 byte clocks. **No arithmetic error in the counts.** Hypothesis (needs hardware doc or
test): the per-line slack is small (36 bytes). Any extra bytes the controller inserts in a line (BTA is
disabled: VIDEO_MODE_FORMAT 0x1e has DISABLE_VIDEO_BTA; escape-mode/LP transitions would cost ~24 byte
clocks = HIGH_LOW_SWITCH_COUNT) cannot be absorbed within one line. I could not determine the DPI FIFO
depth or the controller's start-of-line condition; without them the underrun mechanism cannot be derived
from these numbers. The numbers do say: an underrun of the DPI FIFO (controller reads faster than the
pipe fills) with the pipe's own FIFO fine is a *phase* problem (controller starts the RGB packet before
enough pixels are queued, or the controller lost one line's alignment), not a rate problem.

## 3. Audit of vlv_dsi_pll.c (all CHECKED, cleared unless noted)

| item | location | finding |
|---|---|---|
| ref clock CHV | `dsi_calc_mnp` :77-81 and `vlv_dsi_pclk` :127 | 100000 kHz, N=4 in both compute and readout (upstream ae9ec62bdadc already in). OK |
| M range / lfsr table index | :80-81, :113, :154-164 | CHV M 70..96 -> indices 8..34 of 39 entries; readback searches the table and errors on no match. OK |
| P one-hot encoding | :110, :134-147 | bit 17+P-2, P=6 -> bit 21, inside 9-bit mask; `fls` decode (upstream 5fe8d1dba706 included). OK |
| N encoding | :112, :138-139 | log2 field, 4 -> 2, fits 2-bit mask. OK |
| search rounding | :91-106 | truncating division, picks minimal abs delta; stops early at exact match. Chosen clock is the best available (delta 666 kHz of 366000); no better M/P exists. OK |
| range check | :72 | 300000..1150000 kHz; 366 MHz inside. OK |
| integer overflow | :57, :99, :166-168 | pclk*bpp = 1.47e6; m*ref = 9.6e6; dsi_clock*lanes = 1.47e6; all far below 2^31. OK |
| burst_mode_ratio in non-burst | intel_dsi_vbt.c:846-849; vlv_dsi.c:59, :67 | 100 -> multiplies/divides by 100/100; no effect. OK |
| dsi clock for RGB888/4 lanes | `mipi_dsi_pixel_format_to_bpp` -> 24; :57 | 24/4 = 6x pclk. OK |
| CHV-specific PLL programming | `vlv_dsi_pll_enable` :213-247 | control=0, divider, control w/o VCO_EN, 10-50 us, control with VCO_EN, poll LOCK up to 20 ms (500 us steps). Same sequence for VLV and CHV; CHV-specific bits (P2/P3 mux, DSI1 mux) left 0 = DSI PLL source for port C, with `DSI_PLL_CLK_GATE_DSI1_DSIPLL` for port C. Nothing CHV-specific missing that I can show from code. |
| lock failure handling | :239-243 | `drm_err` and return; the modeset continues with an unlocked PLL (no error propagation). Robustness gap, but the device's kernel log never shows "DSI PLL lock failed", so not active here. Not a bug for this panel. |
| disable | :249-264 | clears VCO_EN, sets LDO_GATE; enable clears it by writing 0 first. OK |
| pre_enable order | vlv_dsi.c:748-749 | PLL disable+enable before `intel_dsi_prepare` (DSI registers need the PLL running). OK |
| readout `config->dsi_pll.ctrl = pll_ctl & ~DSI_PLL_LOCK` | :341 | state checker compares `dsi_pll.ctrl/div` (intel_display.c:5410); matches compute (LOCK masked). OK |
| comment ":56 pixel clock is converted from KHz to Hz" | :55-56 | stale comment, no conversion happens, result is kHz as used. Cosmetic. |

Clock-derived D-PHY values, cross-checked against the dump with bitrate 366666 kbps (vlv_dsi.c:1593-1754):
DPHY_PARAM 0x18b880 = 0x1c0d300a -> exit_zero 28, trail 13, clk_zero 48, prepare 10: all reproduced
(ceil(150*366666/2e6)=28, ceil(70*366666/2e6)=13, ceil(260*366666/2e6)=48, ceil(50*366666/2e6)=10).
HIGH_LOW_SWITCH 0x18b844 = 0x18 = max(21,13)+3 reproduced. LP_BYTECLK 0x18b860 = 3 reproduced.
CLK_LANE_SWITCH_TIME_CNT 0x18b888 = 0x001b000c -> 27/12 reproduced. INIT_COUNT 0x18b850 = 0x7d0 = VBT.
`intel_dsi_bitrate` products (max 310*366666 = 1.1e8) do not overflow int. All OK.

HS_TX_TIMEOUT (known, not mine): `txbyteclkhs(1308*774)` with u16 parameter -> 29352 px -> 22015 = 0x55ff
(dump) instead of 759295 = 0xb95ff. Reproduced; `pixels*bpp*ratio` for the full frame (2.43e9) fits u32
once the parameter is widened, but not int: use u32/`unsigned int` throughout.

## 4. cdclk on CHV (CHECKED; one PLAUSIBLE item)

- Selection (`vlv_calc_cdclk`, intel_cdclk.c:569-587): min_cdclk = `DIV_ROUND_UP(pixel_rate*100, 95)`
  (CHV guardband 95 %, :2908-2920) = ceil(61111*100/95) = **64328 kHz**; plane min cdclk at 1:1 scaling =
  61111 (`vlv_plane_min_cdclk`/`i9xx_plane_min_cdclk`); `vlv_dsi_min_cdclk` (vlv_dsi.c:1756-1779)
  returns **0 on CHV** (320000 only for VLV, 158400 only for GLK); audio/bw/fbc/vdsc contribute 0.
  -> min_cdclk 64328 -> `vlv_calc_cdclk` returns **266667 kHz** (branch `min_cdclk > 0`, not > 266667).
  With no active pipe (panel power-cycled off) it drops to 200000; `output on` brings 266667 back.
  So every panel reset also cycles cdclk 266667 -> 200000 -> 266667.
- Punit/CCK divider for 266667: `DIV_ROUND_CLOSEST(hpll*2, 266667) - 1` = 5 (hpll 800 MHz) or 11
  (1600 MHz); the hpll SKU is not in the data set. `chv_set_cdclk` (:755-800) accepts 333333/320000/
  266667/200000. Max cdclk CHV 320000 (:3777-3778).
- A cdclk change on CHV always forces all pipes off (`intel_cdclk_can_cd2x_update` false for
  DISPLAY_VER < 10, :2562-2568; -> `intel_modeset_all_pipes_late`, :3592-3599). The fresh-boot log shows
  HS_TX_TIMEOUT stayed at the GOP value 0x3fffff while sway ran, i.e. no driver modeset happened at
  boot; therefore the GOP had already programmed the same cdclk the driver computes (266667 by the
  above). The glitch was seen in that state too.
- **PLAUSIBLE (hypothesis, test cheap):** upstream commit c8dae55a8ced (Hans de Goede, 2017,
  "drm/i915/vlv: Add cdclk workaround for DSI") describes, on Bay Trail with a DSI panel at cdclk
  **266667**: "the LCD panel will show an image shifted aprox. 20% to the left (with wraparound) and
  sometimes also wrong colors, showing that the panel controller is starting with sampling the datastream
  somewhere mid-line"; forcing cdclk >= 320000 fixed it. That is the workaround now in `vlv_dsi_min_cdclk`,
  restricted to `display->platform.valleyview`. The symptom description (shifted picture with
  wraparound, wrong colours, panel sampling the stream from the wrong position) is very close to this
  device's desync (split picture, red/blue swapped), the mechanism (pipe->DSI handoff at low cdclk) is
  in exactly the path where the DPI FIFO underrun latch sits, and the GPD runs at 266667 kHz by the
  driver's own arithmetic. There is no documented requirement relating cdclk to the DSI/pixel clock for
  CHV that the driver ignores; this is an empirical workaround that was never evaluated on CHV. I cannot
  show from code that 266667 is wrong on CHV, so this is not a confirmed bug.
  - Verify first (read-only): `cat /sys/kernel/debug/dri/0/i915_cdclk_info` on the device
    ("Current CD clock frequency: ... kHz"); expected 266667 while the panel is on, 200000 while off.
  - Test patch (one line, vlv_dsi.c:1767): `if (display->platform.valleyview || display->platform.cherryview) return 320000;`
    -> cdclk 320000 on CHV with DSI. Side effects: `vlv_program_pfi_credits` (:628-663) picks 63 credits
    instead of 12 if 320000 >= czclk; voltage level/divider 4 instead of 5 (hpll 800). Panel timing,
    DSI PLL and watermark inputs are unchanged (pixel_rate stays 61111). Metric: time to first underrun
    latch and desync frequency, before vs after, as in TASK.md.
  - If it helps, the upstream-ready form is extending the existing comment/condition to CHV with the
    GPD Win as the tested device (the original patch was tested on four VLV devices only).

## 5. Things checked and cleared (so nobody redoes them)

- `intel_dsi_compute_config` (vlv_dsi.c:269-325): clears mode flags, pipe_bpp 24, calls
  `vlv_dsi_pll_compute`, sets `clock_set` so `chv_crtc_compute_clock` (intel_dpll.c:1453-1478) skips the
  DPLL search and returns early for DSI (port_clock/crtc_clock keep the DSI values). OK.
- `intel_dsi_get_config` (vlv_dsi.c:1174-1198): VLV/CHV path only reads the PLL; H/V timings come from the
  pipe registers. Dual-link doubling not applicable (single link, port C only). OK.
- `intel_crtc_compute_pixel_rate` (intel_display.c:2251-2262): GMCH -> pixel_rate = pipe_mode clock =
  61111; feeds watermarks and cdclk. OK (no pfit).
- Dual-link/pixel-overlap code paths (intel_dsi_vbt.c:802-811, vlv_dsi.c:1230-1237, :618-631) inactive:
  VBT "Dual Link Support: not supported", pixel overlap 0.
- Burst-mode code (intel_dsi_vbt.c:817-845) inactive: video mode 2 = non-burst sync events.
- Escape clock: `MIPI_CTRL(PORT_A)` divider 1 (vlv_dsi.c:1330-1333, "shared for A and C"); LP_BYTECLK and
  INIT_COUNT consistent with the dump. OK.
- `DPOUNIT_CLOCK_GATE_DISABLE` is set in pre_enable (vlv_dsi.c:763-764) and preserved by the CHV power
  well code (intel_display_power_well.c:1267-1272, upstream bb98e72adaf9). OK.
- VLV/CHV DSI scanline counter quirk (upstream ec1b4ee2834e, intel_vblank.c:220-235, :694-696) is in
  place; it concerns vblank evasion, not the link clock.
- MIPI port C control: `VLV_MIPI_PORT_CTRL(PORT_C)` = 0x180000 + 0x61700 = 0x1e1700, as in the dump. The
  dump shows 0x1e1700 = 0 and 0x1e1190 (port A) = 0x00030000 (AFE_LATCHOUT | LP_OUTPUT_HOLD, no DPI_ENABLE)
  while the panel runs. `intel_dsi_port_enable` (vlv_dsi.c:633-659) writes DPI_ENABLE into
  `port_ctrl_reg(display, PORT_C)` = 0x1e1700, so either the readout tool does not see that register or
  the hardware does not reflect the write; the clock analysis does not depend on it. Flagged for the
  register-map owner; I did not resolve it.
- Upstream `vlv_dsi_pll.c` at HEAD (af32da41b032) is byte-identical to v7.2.9; nothing clock-related was
  fixed after 7.2 in vlv_dsi.c, intel_dsi_vbt.c or intel_cdclk.c (git log checked).

## Summary of verdicts

1. Static pipe/link rate mismatch: REJECTED (same VCO, fixed 4:3 ratio, 61111 kHz reproduced exactly).
2. DSI PLL divider/rounding/overflow/ref-clock/lfsr/CHV programming bugs in vlv_dsi_pll.c: none found.
3. Byte-clock counts: reproduced exactly; line budget 571.5 of 580.5 byte clocks with 9 byte clocks
   (196 ns) slack if HSYNC_PADDING is not transmitted in sync-events mode (must be the case). No error.
4. cdclk: driver runs CHV at 266667 kHz; the VLV-only >= 320 MHz DSI workaround (c8dae55a8ced) has a
   symptom description matching this device's desync. PLAUSIBLE; one-line test patch proposed.
