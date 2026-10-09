# Round 2 brief: CHV DSI split/flash glitch (GPD Win 1), after the first audit and device tests

Read first: `TASK.md` (round 1 task, rules still apply), `FINDINGS.md` (round 1 results and test results),
`docs/hardware.md` "Display" (device history). This file compiles everything learned so far and states the
open questions. Kernel 7.2.9-arch1-1, `i915`, Cherry Trail (CHV), panel on MIPI port C, pipe B.

## The symptom, precisely
- **Flash**: for about one frame the picture shows wrong content (bottom part at the top), then it is fine.
  Frequent in DevilutionX (several per 10 min), also Zelda 3, Sam & Max. Never seen in Psychonauts (the heaviest
  game) or RetroArch. NOT load-related (DevilutionX is light). All three affected games are X11 via Xwayland
  (DevilutionX SDL2/OpenGL, Sam & Max under Wine); this is a correlation, not tested.
- **Split**: the same misalignment, but it stays: shifted by a line-sized amount (bottom part on top), sometimes
  by a few pixels **with colors rotated** (red/blue swapped) = the pixel stream is offset by a byte count that
  is not a multiple of 3 (RGB888). It can last seconds, about a minute, or until a modeset (`swaymsg output
  DSI-1 power off/on`); some recover by themselves (stock too). Persists after the game quits.
- A `grim` screenshot of the compositor is correct: the corruption is after the framebuffer.

## Measured facts (with stock unless noted; data in `data/`)
1. **Registers**: every MIPI port C register equals the driver's arithmetic; the GOP programs the same values at
   fresh boot (only `MIPI_HS_TX_TIMEOUT` 0x3fffff vs the driver's truncated 0x55ff, and `MIPI_INTR_STAT` 0).
   **No register differs between good and split state** except the sticky `MIPI_INTR_STAT` bit 20
   DPI_FIFO_UNDERRUN (set by flashes too). Snapshots during splits: `data/glitch-bad-regs.txt`,
   `data/split-0002-regs.txt`, `data/split-0005-regs.txt`; fresh boot `data/fresh-boot-regs-2026-10-09.txt`.
2. **Only the DSI DPI FIFO underruns, never the pipe**: PIPESTAT bit 31 stays 0, FIFO underrun reporting is on
   for pipe B in every state (`cpu=yes`, also at boot), no `CPU pipe B FIFO underrun` message ever.
   Watermarks/FIFO split for the plane were checked and are fine (round 1, `reports/C-watermarks.md`).
3. **The DSI controller sets the frame timing** (i915 tracepoints `intel_pipe_update_*` give the hardware frame
   counter and scanline at each flip; `data/trace-2026-10-09/`). Stock: measured period 16.6094 ms over 271 s =
   1308 lines x 582 byte clocks (HACTIVE 540 + HFP/HSYNC/HBP 14 each) at 45.833 MHz (16.6093 ms); the pipe's own
   pixel clock would give 16.5664 ms. So HSYNC_PADDING_COUNT is used in non-burst sync-events mode (the code
   comment says it is ignored), and the pipe is held to the link's pace. With patch 0005 (13/14/13 = 580) the
   period became ~16.552-16.565 ms, i.e. it follows the programmed DSI line length again.
   `VIDEO_MODE_FORMAT` has `IP_TG_CONFIG` set by the driver (0x1e); its meaning is a key question.
4. **Each flash is a one-frame pipe stall**: in the trace, the hardware frame counter advanced by 1 over 33.7 ms
   while the scanline moved only ~34 lines (residual exactly one frame, 16.7 ms). Stock: 2 stalls in 5 min, both
   matching flashes Robin reported (17:02:01, logger caught the DPI underrun latch; ~17:02:34). With 0005: two
   2 s windows at 16.6899 ms (= one extra frame each) when Robin reported flashes. No other timing anomaly.
   Nothing unusual in the i915 events around the stalls: sway flips once per frame at scanline ~450-550 (vblank
   evasion window 1272-1279, never near it), no cursor/sprite/cxsr/FIFO-resplit/watermark events for minutes
   before (`data/trace-2026-10-09/events.log`). The trace has only i915 display events (no GT, CPU, IRQ events).
5. **The frame period drifts slowly** (+~0.4 us/frame per minute, occasional small steps) and does NOT change
   when a split begins or ends: there is still no known "picture is wrong" signal.
6. cdclk is 266667 kHz (GOP and driver); max on CHV 320000. DSI PLL M=88 N=4 P=6, 366.667 Mbit/s/lane,
   pclk 61111 kHz (GOP readback adopted), DSPCLK_GATE_D 0x10000800 (DPOUNIT gating disabled), MIPI_CTRL C 0x18,
   port A EOT_DISABLE 0x100 (bit 8, from GOP), port C ctrl 0x1e1700 reads 0 (documented quirk).
7. Boot is a fastset: the GOP's link state stays until the first driver modeset (a patch only acts after a panel
   power cycle). Splits happen in both the GOP state and the driver state.

## Tested on the device (one-shot test boots, `kernel/i915/`), all negative
| Patch | Change | Result |
|---|---|---|
| 0002 | `vlv_dsi_min_cdclk()` 320 MHz on CHV (as `c8dae55a8ced` does for VLV) | cdclk 320000 confirmed; flashes and splits continue |
| 0003 | `BXT_DEFEATURE_DPI_FIFO_CTR` (EOT_DISABLE bit 9) on port C, as `f90e8c36c886` for BXT | bit reads back 0: not implemented |
| 0004 | same bit in port A's EOT_DISABLE (RMW) | dropped too (still 0x100): CHV has no such bit |
| 0005 | blanking line positions rounded down (DSI line 580 <= pipe 580.5) | period changed as predicted; splits continue (several in 10 min, maybe more often) |
Not tested: 0001 (u16 HS_TX_TIMEOUT fix, a real bug) on its own; a fresh-boot desync already happened with
the long GOP timeout, so it is not expected to help.

## Lessons (method)
- Never write display registers on the running device (a live write hung the display, hard reset needed).
  Test only through a patched module at boot: `kernel/i915/build.sh NNNN` (desktop, ~1 min) +
  `install-test.sh` (one-shot boot entry, stock stays default). Do one panel power cycle after boot.
- Read-only tools on the device: `intel_reg read`, debugfs `i915_display_info`/`i915_cdclk_info`, tracefs i915
  events (`trace-logger.py`), `period-monitor.py`. Robin reports flashes/splits by message; that is the ground
  truth; register latches and timing alone have misled us repeatedly.
- Upstream analogues (Bay Trail cdclk, Broxton FIFO flush) described the same symptom and did not apply.
  Don't trust code comments about what the hardware ignores (HSYNC count).
- The sticky underrun latch is not a "broken now" signal (an auto-reset on it fired during harmless flashes).

## Open questions for the analysis
1. **What makes the pipe stall for exactly one frame**, given the DSI controller drives the timing? What does
   `IP_TG_CONFIG` select (whose timing generator runs the frame), and how do the pipe and the DSI controller
   hand off frame start / vblank on VLV/CHV in video mode? What can make one side miss one frame start? Anything
   the driver does periodically (vblank evasion, PIPESTAT handling, flips, power wells, Punit/DDR DVFS, PSR-like
   features, `vlv_*` power-gating, RC6 interaction with display) that could cause it once every few minutes in
   some games and never in others? What differs for a client that flips every frame vs. one that doesn't?
2. **What leaves the stream byte-misaligned afterwards**, and what could re-align it short of a full modeset
   (DPI_CONTROL commands SHUTDOWN/TURN_ON, DEVICE_READY/ULPS, port ctrl, a forced EOT via HS_TX_TIMEOUT, the
   `MIPI_*_FIFO` controls, `HS_LS_DBI_ENABLE`, etc.)? Is there a register-level way to *detect* the misaligned
   state (counters, FIFO status `MIPI_GEN_FIFO_STAT` bits, `0x1e1708/0x1e170c`, anything)?
3. **Sibling code**: gma500 Medfield (`mdfld_dsi_dpi.c`, `mdfld_dsi_output.c`, same DSI IP), old Intel Android /
   IA kernels for BYT/CHV (`intel_dsi` with underrun handling?), coreboot/EDK2 GOP DSI code, and the public Intel
   PRMs for VLV/CHV display (01.org "Intel Atom Z36xxx/Z37xxx" and "Cherry Trail" graphics PRMs, if they cover
   MIPI). What do they do on DPI FIFO underrun, with IP_TG_CONFIG, hsync padding, and recovery?
4. Concrete next experiments: patches (module at boot only) or read-only measurements that would discriminate
   between hypotheses, ranked by expected information. Especially: which additional tracepoints/registers to
   record at a flash (GT activity, IRQ timing, PUNIT) to find what precedes a one-frame stall.

## Round 2 results (2026-10-09)
`reports/R2A-driver-mechanism.md`, `reports/R2B-external-sources.md`. Key: fdo bug 105834 (GPD Pocket) = same
symptom, page-flipping clients only, gone on a replacement mainboard. Merged next steps (cheapest first):
1. E2/E3: provoke or suppress PMIC-bus (charger) reads during play; E1 trace flashes vs Punit/PMIC windows.
2. Flipping vs non-flipping client A/B for the same game.
3. Patch: EOT_DISABLE recovery-disable bits 2-7 + 0001. 4. Patch: no DDR DVFS (then no PM5).
5. Patch: frame start delay 4 lines. 6. Read-only GEN_FIFO_STAT vs PIPEDSL sampling as a state detector.
Run each on the stock module (reboot normally first) with the residual flash detector.

- 2026-10-09 18:23: 0007 (no DDR DVFS) tested: underrun + two one-frame stalls within 40 s. Refuted.
- 2026-10-09 18:28: 0001+0006 tested (bits stick: EOT_DISABLE 0xfc; HS_TX_TIMEOUT 0xb95ff and MIPI_INTR_STAT bit 21
  no longer set after the modeset, confirming it came from the truncated timeout). Underrun 27 s in, split held.
  Refuted. That underrun came WITHOUT a one-frame pipe stall (period windows normal, trace residual 0) while the game
  was barely flipping (241 flips in 20 s, a 6 s gap): stalls are not required for a split.
- 2026-10-09 18:32-18:41: 0008 (frame start delay 4) tested: 3 flashes in 9 min (stock ~2 in 5), first after 4.5 min.
  Flash rate unchanged; held split after ~11 min. No effect. (Note: Intel's formula (htotal*5ns*delay >= 8000 ns) gives 3, not 4; with the real 12.7 us line even
  delay 1 exceeds 8 us.) All round-2 patches (0006, 0007, 0008) refuted. Remaining: read-only/no-kernel tests E1-E3
  (PMIC-bus polling), flipping vs non-flipping client A/B, GEN_FIFO_STAT vs PIPEDSL sampling.
