# Report R2B: CHV DSI split/flash glitch — sources outside the mainline i915 tree (2026-10-09)

Round 2, open question 3 of `BRIEF-round2.md`. Written by a Fable analysis agent; saved verbatim in substance.
Downloaded material was in the session scratch dir (not kept): PRM PDFs, Wayback copies of bugzilla, Intel's
`ProductionKernelQuilts`, Intel Android CHT kernel sources, gma500 Medfield, ChromeOS 3.18 `intel_dsi.c`.
Conventions: [source] = what a document says; [inference]; [relevance] = fit to our symptom.

## 0. Executive summary
1. **The public Bay Trail PRM Vol 10 "Display" (2014) documents the whole VLV MIPI register block** (pp. 351-422,
   https://www.x.org/docs/intel/BYT/intel_os_gfx_prm_vol10_-_display.pdf). The public Cherry Trail/Braswell PRM
   Vol 12 "Display" (2015, https://www.x.org/docs/intel/CHV/intel-gfx-prm-osrc-chv-bsw-vol12-display.pdf) does
   **not mention MIPI at all**. The VLV text settles register meanings (IP_TG_CONFIG, EOT_DISABLE "recovery
   disable" bits, HS_TX_TIMEOUT, DPI FIFO underrun definition, GEN_FIFO_STAT bit 28, PORT_CTRL D-PHY delay knobs,
   MIPI_CTRL). It documents **no recovery for DPI FIFO underrun and no DSI mode-set sequence**. PIPECONF bits
   28:27 (frame start delay, which i915 programs) are "Reserved" in the public doc.
2. **freedesktop bug 105834 (GPD Pocket, Z8750, 2018) is our exact symptom**: shift with wrap + color-channel
   rotation, persists across X restart and into the boot splash, fixed by DPMS off/on or a mode change, triggered
   only with a page-flipping client (xf86-video-intel TearFree); modesetting DDX "immune" in >6000 s of tests;
   kernel silent. Ville Syrjälä: "the DSI encoder gets confused somehow and starts sending data out of phase. I
   could imagine that a FIFO underrun could perhaps lead to that outcome". Closed NOTABUG because **a replacement
   mainboard did not reproduce it** (per-unit marginality).
3. **Intel's own Android CHT production kernel (3.14 quilts) attributes "sporadic screen shift / split screen on
   DSI" to a DPI FIFO underrun at port enable** and fixes it only by ordering/waiting (FIFO-empty wait before
   `DPI_ENABLE`, display-off after SHUTDOWN). No vendor code (that kernel, its ADF driver, ChromeOS 3.18, gma500
   Medfield) has any runtime DPI-underrun detection or resync; Medfield's error handler treats bit 20 as "No Action
   required". Nothing re-aligns a running video stream short of disabling the port.
4. Coreboot/EDK2/Windows: nothing public about CHV DSI programming. Negative result.
5. Documented knobs mainline never touches, cheap to test at boot (ranked in §6).

## 1. Register facts (VLV PRM; same Arasan DSI host IP as CHV; offsets match ours)
- **MIPI_INTR_STAT bit 20 DPI FIFO Underrun** [p.352]: "Set to '1' if there is no data in the dpi fifo to make a in
  time delivery of the pixel data to the DSI receiver". Bit 21 HS TX Timeout: "Set if a high speed transmission
  prevails for more than the expected count value". Bits 18/19 high/low contention. Bits 0-13 and 24-26 are
  **panel-reported** errors from the acknowledge packet (SoT/EoT sync, ECC, checksum). [inference] They need a bus
  turnaround; the driver sets `DISABLE_VIDEO_BTA`, so we can never see whether the panel reports sync errors.
- **MIPI_VIDEO_MODE_FORMAT (0xB058)** [p.365]: bit 4 random resolution defeature; bit 3 disable video BTA (BTA at the
  last VFP line); **bit 2 IP TG Config: "the DSI controller should discontinue the DPI transfer after the last line
  of the VFP after ip_tg_enable deassertion. 0 - ... stops the DPI transfer immediately after the current packet"**.
  [inference] IP_TG_CONFIG does NOT select the timing master; it only governs stream end on port disable.
  **PORT_CTRL bit 31** [p.383]: "When it is enable it starts to generate timing for this MIPI port" — the MIPI port
  generates the DPI timing, consistent with the measured link-paced frame period. Pipe<->DSI flow control and the
  effect of an underrun on the packetizer are not documented.
- **MIPI_EOT_DISABLE (0xB05C)** [pp.366-368]: bit 0 EOT disable; bit 1 clock stop; bit 2 TX ECC multibit error
  recovery disable; bit 3 TX DSI type not recognised recovery disable; bit 4 high contention recovery disable;
  bit 5 low contention recovery disable; **bit 6 HS TX timeout error recovery disable: "0 - HS_Tx_timeout error
  recovery action will be taken by the DSI Tx controller if the processor clears the HS_Tx_timeout error
  interrupt. 1 - recovery action will not happen ... informative interrupt"**; bit 7 LP RX timeout recovery
  disable. Bits 31:8 reserved (BXT's bit 9 is a BXT addition, consistent with tests 0003/0004). Intel's ADF
  driver programs all of bits 0-7 from VBT (`dsi_dpi.c:113-120`); mainline only EOT/CLOCKSTOP.
- **MIPI_HS_TX_TIMEOUT (0xB010)** [p.358]: "maximum duration allowed for the DSI host to remain in high speed mode
  for a transmission. If the counter expires, HS mode is terminated with EOT and the lanes enter stop state".
  Medfield programs `vtotal*htotal*bpp/(8*lanes)` or 0xffffff; Intel's Android i915 used u32 `DIV_ROUND_UP_ULL`
  (never had the u16 bug).
- **MIPI_GEN_FIFO_STAT (0xB074, RO)** [p.371]: bit 28 DPI FIFO empty (default 1); no fill level or error flag.
- **MIPI_DBI_RESOLUTION (0xB024)** bits 1:0 DBI FIFO throttle: the only documented stall/flow control, for DBI only.
- **MIPI_CTRL (0xB104)** [p.376]: bits 6:5 escape clock divider, 4:3 read-request priority (11 = high), bit 2 RGB
  flip. Our 0x18 = high priority, BGR.
- **MIPIA_PORT_CTRL (0x61190)** [pp.383-386]: bit 31 EN (timing generator); bits 30:27 "MIPI4DPHY AdjDly HSTX MIPI A
  ... buffer delays on the ckdsi2x clock going to the six flops storing the HS TX data and clock ... Will need to
  set these bits to a value determined by clock timing team"; 21:18 FLISDSI AdjDly; bit 23 SelFlopped HSTX; bit 17
  AFE latchout (RO); bit 16 LP output hold; **bits 15, 14:11 and 7:5 = the same AdjDly fields for MIPI C** (port
  C's HS-TX delay knobs live partly in port A's register); bits 1:0 lane configuration. [inference] Mainline writes
  port C's (unreadable) register with DPI_ENABLE only, so any GOP delay value there is lost at the first modeset;
  port A reads 0x00030000 (all delay fields 0).
- **PIPECONF** bits 28:27 "Reserved" publicly; i915 programs frame start delay 1; Intel's Android kernel: "Clear any
  frame start delays used for debugging left by the BIOS".
- No MIPI-specific mode-set sequence in the public document.

## 2. Intel Android-IA / ChromeOS / gma500
- **Intel ProductionKernelQuilts** (https://github.com/intel/ProductionKernelQuilts, `uefi/cht-m1stable/patches/`):
  - `0010-MUST_REBASE-VPG-drivers-video-adf-Fix-sporadic-scree.patch` (2015, GMINL-5611): "display off sequence
    needs to be send after shutdown packet otherwise sporadically split screen issue may occur"; squash
    `0014-...Fix-screen-shift-on-MIP...`: "sporadic shift seen on MIPI after resume, **due to DPI FIFO underrun**,
    fixing the issue by giving delay for the FIFO to be empty" (wait before setting DPI_ENABLE). Mechanism
    statement relevant; their trigger was port enable, ours is runtime.
  - `0001-FOR_UPSTREAM-...Add-DPI-AND-DBI-FIFO-empty...`: wait for DPI/DBI FIFO empty at the start of post_disable
    (mainline lacks it; disable path only).
  - `0081-REVERTME-...Updated-DSI-sequence`: HW/SV-team sequence for issues "which might stall the pipe": port
    enable before pipe and planes (mainline already does this).
  - `...Allow-the-cd_clk-to-run-at-266...`: "CHT systems can work only when CDclk freq is equal to OR higher than
    CZclk freq": 266 MHz is an intended operating point with DSI (consistent with test 0002).
  - `0013-...reset-the-display-hw-if-vi...`: video->command mode switch needs a Punit display power-gate reset.
  - `0007/0004-...MIPIC-port...`: "CHT till C0 steppings has hardware bug that MIPI port-C register can[not] be read".
  - `0205-...chv-Add-workaround-for-MIPI-la...`: "A0-A3 silicon has bad clock timing on the MIPI data lanes ...
    jittering the data lanes": ORs 0xe82d0000 into MIPI_PORT_CTRL on CHV. Pre-production only; precedent that the
    PORT_CTRL HS-TX delay fields are Intel's knob for marginal D-PHY timing.
  - No patch references DPI_FIFO_UNDERRUN at runtime, resync, or MIPI_INTR_STAT polling.
- **Intel Android CHT kernel** (https://github.com/fxsheep/cht_kernel, 3.14): i915 DSI programming like mainline
  2015 (IP_TG_CONFIG, INTR_* = 0xffffffff, EOT_DISABLE only EOT/CLOCKSTOP, h-segments rounded up, HS_TX_TIMEOUT
  u32 full frame); no MIPI IRQ handling. ADF `common/dsi` (Moorefield): reset-recovery loop for failed init,
  **PIPECONF frame start delay from an HW-team formula "(htotal * 5ns * hdelay) >= 8000ns ... max hdelay is 4"**,
  EOT_DISABLE recovery mask from VBT. DPI_FIFO_UNDERRUN only printed by a debug dumper.
- ChromeOS 3.18 `intel_dsi.c`: upstream backports only.
- gma500 Medfield: `handle_dsi_error()` bit 20 "No Action required"; byte-clock counts floored; HS_TX_TIMEOUT full
  frame or 0xffffff; no recovery path for a video-mode stream fault.

## 3. Same symptom elsewhere
- **freedesktop bug 105834 "display corruption on GPD Pocket (Cherry Trail Atom x7-Z8750)"** (2018, NOTABUG; Wayback
  https://web.archive.org/web/2020/https://bugs.freedesktop.org/show_bug.cgi?id=105834): "the screen shifts
  vertically ... and possibly the colors change (swapping of color channels"; "stays there, down to the splash
  screen when powering down"; screenshot correct; only the DSI output, HDMI fine; kernel silent with drm.debug.
  Ville: "The corruption apparently happens when just page flipping. That sort of rules out straightforward bugs in
  the DSI enable sequence ... the DSI encoder gets confused somehow and starts sending data out of phase. I could
  imagine that a FIFO underrun could perhaps lead to that outcome". DPMS off/on fixes it; Windows 10 never showed
  it. **Replacement mainboard: not reproducible.** Reporter's log (https://sobukus.de/gpd/displayglitch/README.txt):
  xf86-video-intel with page flips glitches in 1-900 s; **modesetting DDX 1800 s and 6310 s without a glitch**;
  "often, the glitch will be quickly triggered and subsequently corrected again by another trigger event". Also
  https://github.com/nexus511/gpd-ubuntu-packages/issues/30 (no compositor for hours: no corruption).
  [relevance: very high] Same four signatures; per-unit susceptibility; flipping client triggers it.
- `ec1b4ee2834e` "drm/i915: Workaround VLV/CHV DSI scanline counter hardware fail" (bug 99086, Asus T100HA): "The
  scanline counter increment is not lined up with the start of vblank ... about 1/3 of a scanline ahead of the
  start of vblank (which is where all register latching happens still)". The one upstream admission that
  pipe<->DSI timing on VLV/CHV is unusual.
- `c8dae55a8ced` (BYT cdclk, tested as 0002: no effect) and `f90e8c36c886` (BXT "mipi_dpf_vblank_start ... DPI FIFO
  is flushed out at the end of each frame"; bit absent on CHV) — the clearest vendor description of the mechanism
  class: vblank start handshake can fire before the frame's bytes left the DPI FIFO; leftover bytes offset the next
  frame.

## 4. Coreboot / EDK2 / GOP / Windows
Braswell coreboot = FSP + binary GOP; nothing public on MIPI programming; no Windows tunables found.

## 5. Synthesis [inference]
1. Timing master: the MIPI port (PORT_CTRL bit 31) generates the timing; the pipe is throttled to the link.
2. Mechanism class: an underrun / early vblank-start leaves a byte offset in the DPI FIFO/packetizer; following
   frames pack from the wrong boundary (fixed shift, R/B rotation); a later underrun can shift it back. Vendor
   drivers "fix" it only by disabling the port.
3. Per-unit susceptibility points at marginal electrical/timing behaviour; software changes only the rate.
4. Why a flipping client: unexplained; candidates are flip latching at vblank start vs the controller's own frame
   start.

## 6. Ranked ideas (patched module at boot only; one at a time)
1. EOT_DISABLE recovery-disable bits 2-7 (0xfc) on port C, with patch 0001. If flashes/splits change, a hardware
   "recovery action" is the one-frame stall.
2. PIPECONF frame start delay 4 lines instead of 1 for VLV/CHV DSI (Intel ADF HW-team rule gives 4 for htotal 774).
3. Read-only high-rate sampling of MIPI_GEN_FIFO_STAT bit 28 and full MIPI_INTR_STAT around flashes.
4. Diagnostic: clear DISABLE_VIDEO_BTA so the panel's acknowledge errors become visible (risk: short TA/LP-RX
   timeouts; panel BTA support unknown).
5. PORT_CTRL HS-TX delay fields for MIPI C (D-PHY margin; undocumented on CHV; could blank the panel).
6. Punit display power-gate reset only as a softer resync than the sway power cycle.
7. Flipping vs non-flipping client A/B on the device (bug 105834's strongest correlation), no kernel change.
