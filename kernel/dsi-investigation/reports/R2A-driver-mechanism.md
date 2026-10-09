# Report R2A: driver-side mechanism (round 2, 2026-10-09; condensed from the Fable agent's report)

- CONFIRMED from the stock trace: converting flips to hardware position (frame*1308+scanline) vs time, the residual
  is 0.00 +- 0.43 lines everywhere except two events of exactly -1308 lines (568.09 s, 601.51 s): one full frame lost,
  no partial stalls, no precursor in the second before. A one-frame pipe stall and a skipped frame start are
  indistinguishable in this data.
- CONFIRMED: no pipe clock is programmed for DSI on VLV/CHV (only the DSI PLL); the pipe is slaved to the DSI
  controller's timing generator ("ip_tg", PORT_CTRL bit 31). The 580 vs 580.5 difference is irrelevant (fits 0005).
  Ville's 8f4d38099b30: on VLV/CHV DSI the scanline counter runs ~1/3 line ahead of vblank start (DPI FIFO depth?).
  DPOUNIT clock gating "can stall pipe" (disabled, OK). VLV gets GCFG_DIS clock-gating WA, CHV does not.
- IP_TG_CONFIG = gma500's COMPLETE_LAST_PCKT / DSI_DPI_COMPLETE_LAST_LINE; the BYT PRM (R2B) says it only governs
  how the stream ends after ip_tg_enable deassertion. Always set by every driver and the GOP.
- Mechanism hypothesis: any pipe-side hiccup longer than the DPI FIFO (~4 us) leaves the pipe behind the TG; it
  resyncs one frame later (flash) or not (split with a byte offset). Same event, different resync outcome.
- Per-frame difference of a flipping client: vblank irq enabled/disabled every frame (vblank_disable_immediate).
- Periodic Punit-related candidates (timing in minutes fits): (1) PMIC-bus semaphore: every transfer on the
  Punit-managed PMIC I2C bus blocks the Punit's PMIC tasks (iosf_mbi_block_punit_i2c_access) + forcewake; the
  statusbar reads battery/charger every 60 s, low-battery.timer every 2 min. Device check 2026-10-09: the charger
  bq24190 sits behind the Whiskey Cove PMIC (i2c-5, INT34D3, likely the Punit-managed bus); the fuel gauge
  max17047 is on i2c-0 (not the PMIC bus). (2) DDR DVFS: watermark level 2 lets the Punit switch DDR frequency;
  bursty light loads (DevilutionX) switch often, constantly busy GPUs (Psychonauts) keep DDR high.
- 0x1e1708/0x1e170c bits 31/24 (/12) differ good vs bad; unknown registers; log them as data.
- Detection ideas: frame-position residual detects every flash; MIPI_GEN_FIFO_STAT bit 28 (DPI FIFO empty)
  sampled against PIPEDSL might distinguish aligned vs split state (read-only).
- Ranked experiments: E1 trace flashes vs PMIC-bus/Punit windows (i2c tracepoints + kprobes on
  iosf_mbi_block_punit_i2c_access, chv_set_memory_dvfs); E2 provoke: read the charger every 0.2 s while playing;
  E3 suppress battery polling for 30 min; E4 clear IP_TG_CONFIG (patch; lower after the PRM text); E5 disable DDR
  DVFS (then PM5) via vlv_setup_wm_latency (patch); E6 GEN_FIFO_STAT vs PIPEDSL sampling; E7 is the pipe underrun
  bit alive in DSI mode (CPU/memory load); E8 GT/CPU/IRQ context at a flash; E9 re-align by DPI_ENABLE off/on.
