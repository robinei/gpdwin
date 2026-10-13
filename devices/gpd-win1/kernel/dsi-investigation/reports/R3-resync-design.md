# Report R3: mechanism and resync design (Fable agent, 2026-10-09; condensed)

**Mechanism (hypothesis fitting all data, one parameter k = bytes the pipe fell behind at an underrun):**
the MIPI port timing generator (TG) paces the pipe through the DPI FIFO. An underrun leaves the pipe k bytes behind.
- k fits in the FIFO (lone underrun = split): the pipe writes its last k bytes during the TG's vertical blanking and
  still catches the next TG frame start. The k bytes stay in the FIFO: every frame starts with the previous frame's
  tail, offset k bytes (rotated colors if k mod 3 != 0). CHV has no end-of-frame FIFO flush (BXT bit 9). The state
  holding the offset is the DPI FIFO occupancy at the frame boundary.
- k exceeds the FIFO (pair = flash): the pipe cannot finish line 1279, parks there, misses the TG frame start; the TG
  sends the leftover bytes and runs dry (2nd underrun), the pipe restarts at the following TG frame start with an empty
  FIFO: aligned. PIPEDSL raw 1279 = first vblank line / frame counter increment point (PRM).
- Cure: one TG frame read while the pipe writes nothing, then the pipe restarts at a TG frame start.

**Patch 0012** (on top of 0011): classify each underrun as PAIR (2nd underrun or stall within 60 ms) or LONE; pipe stall
detector in the poller; drm_vblank_work sampling MIPI_GEN_FIFO_STAT bit 28 (DPI FIFO empty) a few lines into vblank
(aligned: empty; split: stale bytes); on a LONE underrun a resync under the crtc + plane modeset locks after the last
commit's hw_done/flip_done:
- method 1 (default) "pipe skip": planes off, vblank off, TRANSCONF disable, wait STATE_ENABLE clear, dwell 3 ms
  (TG drains the FIFO), TRANSCONF enable (retry up to 3x if PIPEDSL does not move), vblank on, planes on;
- method 2: DSI port (DPI_ENABLE) off, wait DPI FIFO empty, on (smaller; unknown whether a TG stop flushes the FIFO).
Module parameters (runtime-writable): vlv_dsi_resync 0 log / 1 lone (default) / 2 FIFO detector / 3 both;
vlv_dsi_resync_method 1/2; vlv_dsi_resync_max 50; vlv_dsi_resync_dwell_us 3000. No panel command or power sequence,
no write to HS_TX_TIMEOUT, only bit 20 of INTR_STAT cleared. Log lines: `classified: LONE/PAIR`, `DSI pipe B stall`,
`DPI FIFO in vblank now EMPTY/NOT EMPTY`, `DSI resync #n (...)`, `result after 1 s: ALIGNED/STILL MISALIGNED`.
~700 lines because of instrumentation; a final version would be ~200.
Its prevention suggestions (forcewake held, C-states blocked) were already ruled out in round 1.
