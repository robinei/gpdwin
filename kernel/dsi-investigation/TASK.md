# Task: find and fix bugs behind the DSI display glitch (i915, Cherry Trail / GPD Win 1)

You are working on the desktop (`~/Code/gpdwin`, clone of the repo that maintains a GPD Win 1 handheld;
read `CLAUDE.md` first, its rules apply). The device cannot build kernels comfortably, you can. Robin
(they/them) installs and tests anything on the device; you never touch the device's running display.

## Goal
Deeply audit the Linux i915 code that drives this device's DSI panel and fix **clear bugs** you can
demonstrate from the code. Deliver patches against the kernel version the device runs (`v7.2.9`, Arch
`7.2.9-arch1-1`) that Robin can test by DKMS/prebuilt module plus reboot, and that are fit to send
upstream. We do **not** know that any specific bug is the cause of the glitch; do not claim a fix without
a test result. Report confirmed bugs, plausible ones and rejected ones separately.

## Hardware and symptom
- GPD Win 1: Atom x7-Z8700 (Cherry Trail, CHV, Gen8 graphics), 5.5" 720x1280 portrait DSI panel
  (MIPI port C, 4 lanes, RGB888), compositor sway rotates to 1280x720. Display is on **pipe B**
  (`[CRTC:86:pipe B]`). Kernel 7.2.9, driver `i915` (in the initramfs `MODULES`, see `data/boot-entry.txt`).
- Symptom: under load (DevilutionX, Zelda 3, Sam & Max; not seen in RetroArch, Psychonauts) the
  picture momentarily flashes (frequent) and sometimes **desyncs persistently**: the picture is split
  (bottom half shown on top), sometimes with red/blue swapped, and stays that way, even after the
  game quits. Fix is a panel power cycle (`swaymsg 'output DSI-1 power off'` / `on`). A `grim`
  screenshot of the compositor looks normal, so the corruption is in the display engine / DSI link, after
  the compositor. Looks like the pixel stream losing alignment with the frame start.
- Kernel log is silent at the time (no errors). The only difference found between good and bad state:
  `MIPI_INTR_STAT` (0x18b804) bit 20 `DPI_FIFO_UNDERRUN` set. It is a sticky latch, set also by harmless
  flashes, so it does not mean "currently broken". `MIPI_INTR_STAT` bit 21 `HS_TX_TIMEOUT` is set
  essentially always after the first driver modeset. See `data/glitch-*` (register dumps, bad vs good) and
  `data/underrun-log-fresh-boot.log`.

## What is already established (do not redo; details in `docs/hardware.md` "Display")
- **Real bug found, but NOT the cause of the desync:** in `drivers/gpu/drm/i915/display/vlv_dsi.c`,
  `txbyteclkhs()` and `pixels_from_txbyteclkhs()` use `u16` for pixel counts and return values.
  `MIPI_HS_TX_TIMEOUT` is computed from `crtc_vtotal * crtc_htotal` (774 x 1308 = 1,012,392 px) which
  wraps to 29,352, giving 0x55ff (observed in the hardware) instead of ~0xb95ff (intended, about one
  frame). Upstream master still has it. Refuted as the desync cause: a desync happened with the register at
  the fresh-boot value 0x3fffff (firmware/VBT value, no driver modeset yet). Still fix it and send it up,
  other timeouts computed the same way (check LP_RX/TA/etc. timeouts, `intel_dsi_get_modes`, etc.) may
  truncate too.
- Machine state in good and bad states is otherwise identical: same plane, fb (XR24, X-tiled), rotation 0,
  no GPU hang (`i915_wedged` 0, empty error state).
- Ruled out by testing: direct scanout, GPU RC6/runtime PM, CPU deep C-states, GPU frequency changes,
  the game's own mode switches, Wine/DXVK/Vulkan (native SDL/OpenGL games do it too), FBC/PSR/DMC (absent).
- A memory-bandwidth load (two processes copying 48 MB buffers) on top of the running game for 3 minutes
  caused **no** underrun (`data/load-test.log`, `data/underrun-log-fresh-boot.log`); a CPU-only load was
  not run. Natural underrun intervals during play: ~1.5 to 2 min. Treat memory contention as unproven.
- **Live register writes are dangerous**: writing `0x18b810` on the running system led to
  `flip_done timed out` and a hard lock within a minute. Do not suggest live pokes as tests.
- Register-map caution: `intel_reg` on the device reads DSI port C control (0x1e1700 by `vlv_dsi_regs.h`)
  as 0 in every state although the panel works; `0x1e1708/0x1e170c` change between reads. We do not
  understand that mapping; check whether the driver's `_MIPI_PORT()` offsets and our base (0x180000) agree.
- Related prior art (see docs/hardware.md "Prior art"): another project's patch
  `drm/i915/chv: Retry stalled DSI transcoder enable` (resume leaves the transcoder enabled but the scanline
  counter stopped, `flip_done timed out`). Different symptom, same code area; worth reading when auditing the
  enable sequence.

## Where to look (ordered by my suspicion, all unverified)
1. **FIFO underrun and its handling.** Why the DPI pixel FIFO runs dry: CHV display watermarks and
   drain latency (`display/i9xx_wm.c`, `chv_*`/`vlv_*` functions: DDL, PND deadline, `FW_BLC`/`DSPFW`
   values, precision, whether they are computed for the DSI pipe's actual pixel rate/mode, cursor, sprite
   planes, and "trickle feed"), the cdclk/DSI PLL clock relationship (`vlv_dsi_pll.c`: is the DSI PLL
   and pixel clock really matching `crtc_clock`, dividers, rounding), and the underrun interrupt path
   (`intel_fifo_underrun.c`: on CHV is the DSI port's underrun in `MIPI_INTR_STAT` ever read or cleared?
   the driver seems to arm interrupts it never handles; check `MIPI_INTR_EN`, `vlv_dsi.c`, `i915_irq.c`).
2. **What an underrun does to the link.** In non-burst mode with sync events the DPI interface needs a
   continuous pixel stream; check what the driver programs for `MIPI_DPI_CONTROL`, `MIPI_DPHY_PARAM`,
   `MIPI_DSI_FUNC_PRG`, `MIPI_VIDEO_MODE_FORMAT`, `MIPI_CTRL`, `MIPI_TRANSCONF`, `MIPI_*_COUNT`
   registers (blanking packet sizes, hsync/hbp/hfp/vsync counts, byte clock conversions, `bpp` and lane
   rounding, off-by-one or unit mistakes), compared with the VBT in `data/decode.txt` / `data/vbt.bin`
   and with the Intel PRMs if you can find them. Also whether an underrun leaves any state the driver
   could reset or resync without a full modeset.
3. **Enable/disable/modeset sequence** in `vlv_dsi.c` and `intel_dsi_vbt.c` (pre_pll_enable, enable,
   pre_enable, disable, post_disable; panel init sequence ordering and timing: delays, the
   `MIPI_SEQ_*` handling, reset GPIO handling, whether LP commands are sent while HS video is running,
   `wait_for_*` timeouts that are too short or ignore failure, missing barriers/`posting reads`).
   Look at the git history of these files (`git log -p` upstream) for fixes that postdate or were dropped
   from 7.2.9, and for commits mentioning CHV/VLV DSI, "underrun", "tx timeout", "dpi".
4. **Anything else you see** that is plainly wrong in this path (integer width/overflow, signedness,
   wrong register or bit name, wrong units, unchecked return values, copy-paste errors).

## Rules for what you may call a bug
A bug needs: the exact code location, why it is wrong (arithmetic, spec, or contradiction with the
hardware state we measured), and the concrete value it produces on this panel (use the VBT and mode:
720x1280, `crtc_htotal` 1308, `crtc_vtotal` 774, 60.253 Hz, 4 lanes, RGB888, non-burst sync events).
Anything else is a hypothesis; label it as such. Do not "fix" things only because upstream does it
differently.

## Deliverables (commit them in this repo)
1. `kernel/dsi-investigation/FINDINGS.md`: per finding: location, evidence, computed values on this
   panel, confidence (confirmed bug / plausible / rejected), proposed change. Include anything you
   checked and cleared, so nobody redoes it.
2. One patch per distinct fix, in `kernel/i915/NNNN-*.patch`, against pristine v7.2.9 (fetch from
   kernel.org or `pkgctl repo clone linux`; pin the checksum in `kernel/i915/sources.sha256` as
   `kernel/rt5645/` and `kernel/intel-sst/` do). Each commit message upstream-ready (what, why, effect,
   `Fixes:` tag if you can identify it).
3. A DKMS or prebuilt-module recipe following `kernel/prepare.sh` / `kernel/install.sh` (see the existing
   `gpd-audio` package). Important: `i915` is in the initramfs `MODULES` and a broken display module can leave the
   handheld without a screen. The recipe must (a) build the module for the exact running kernel, (b) keep
   the stock module as a fallback (separate boot entry or an easy removal from the initramfs by editing the
   boot entry from another machine/ssh), (c) not run anything on the device by itself; write the exact
   commands for Robin to run. ssh to `gpdwin.lan` is available; do not install, reboot or restart anything
   there without asking Robin.
4. A test plan Robin can follow. The read-only logger `data/watch-readonly-logger.py` (needs root, run on
   the device) logs underrun-latch edges and the HS timeout register with timestamps; a panel reset
   (`swaymsg 'output DSI-1 power off'`, 2 s, `power on`) re-arms the latch. Suggested metric: time to the
   first underrun after a panel reset while playing the same scene, before vs after, several repetitions;
   plus the desync frequency over a long play session. Report honestly if the patch changes nothing.
5. Update `docs/hardware.md` (Display), `docs/changelog.md`, run `scripts/sync check`, commit and push.

## Do not
- Send anything upstream or post to a bug tracker; draft the text and let Robin decide.
- Store keys or passwords; reboot/suspend the device; write registers on the device.
- Over-claim. We were wrong about this display several times already (an earlier theory was refuted by a
  single observation). Prefer "the code does X, which is wrong because Y" over "this explains the glitch".
