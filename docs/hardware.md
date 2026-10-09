# Hardware

GPD Win 1 (2016): Atom x7-Z8700 (Cherry Trail, 4 cores, 1.6 GHz), 3.7 GB RAM, 58 GB eMMC,
5.5" 720x1280 DSI panel (portrait native), AMI Aptio BIOS 5.11 (Delete key, or Pegasus
"BIOS Setup"). 64-bit UEFI.

| Part | Driver | Firmware / notes |
|---|---|---|
| GPU (HD Graphics, CHV) | i915 | none needed; Mesa GL 4.6 |
| Wi-Fi BCM4356 (PCIe) | brcmfmac | `linux-firmware-broadcom`: `brcmfmac4356-pcie.bin` + `brcmfmac4356-pcie.gpd-win-pocket.txt` |
| Bluetooth BCM4356A2 (UART) | hci_uart_bcm | patch `brcm/BCM4356A2.hcd` missing (not in repos). Radio rfkill-blocked. |
| Audio RT5645 | intel_sst_acpi (legacy SST, not SOF) | `linux-firmware-intel` (`intel/fw_sst_22a8.bin`), `alsa-ucm-conf`. Kernel detects "GPD Win / Pocket platform". |
| Gamepad | xpad | `Microsoft X-Box 360 pad` (045e:028e), Guide = BTN_MODE (316). Only in gamepad mode; the hardware switch's other mode is "Mouce for Android" (0079:1a00) |
| Touchscreen | Goodix | works, rotation correct |
| Accelerometer | kxcjk1013 | unused |
| Battery / charger | max170xx_battery, bq24190-charger | `capacity`, `status`; charger `online` = 1 on power |

## Fixes
| Problem | Fix | Where |
|---|---|---|
| Backlight: `Failed to get the SoC PWM chip`, no `intel_backlight` | load LPSS PWM before i915 | `MODULES=(pwm-lpss-platform i915)` in `/etc/mkinitcpio.conf` |
| Portrait panel | console and sway rotation | kernel `fbcon=rotate:1`; sway `output DSI-1 transform 90` |
| RetroArch fullscreen at 720x1280 | it reads the panel's native mode | `video_fullscreen_x/y = 1280/720` |
| Suspend aborted: `xhci_hcd ... hcd_pci_suspend returns -16` | disable wakeup on the USB3 controller (pad/keyboard can't wake it; power button and lid still do) | `/etc/udev/rules.d/90-xhci-no-wakeup.rules` |
| Backlight at minimum after hibernate resume | save/restore around sleep | `/usr/lib/systemd/system-sleep/backlight` |
| Previous install froze after resume | not seen since the xHCI fix: 5/5 s2idle cycles, lid suspend-then-hibernate, plain hibernate all fine | — |
| Wi-Fi regulatory domain missing (`regulatory.db failed`) | `wireless-regdb` + country | `/etc/modprobe.d/cfg80211.conf`: `ieee80211_regdom=NO` |

## Known harmless kernel messages
- Hibernate: `i2c i2c-5: Transfer while suspended` + WARNING trace in `__i2c_smbus_xfer`, then
  `ACPI Error: Aborting method \_SB.P18W._ON`. Firmware powers a rail via the PMIC before the
  I2C bus is resumed. Resume works.
- `cherryview-pinctrl INT33FF:04: probe ... failed with error -61`, `intel-hid: failed to enable
  HID power button`, `supply ... not found, using dummy regulator`, `hpet`, `esrt`, `TDX`.
- brcmfmac: `no txcap_blob available`, `brcmf_p2p_send_action_frame: Unknown Frame`.
- After hibernate: `usb ...: WARN: invalid context state for evaluate context command`.
- Bluetooth: `BCM: firmware Patch file not found`.

## Display
- Only mode: 720x1280 @ 60.253 Hz, rotated to 1280x720. ~267 ppi, sway scale 1.
- A game's fullscreen switch sometimes leaves the picture split (bottom half on top); seen with
  DevilutionX, Zelda 3 and Sam & Max (2026-10-09, also with red/blue swapped; it persisted into
  Pegasus after the game quit). It is in the display engine, after the compositor: a `grim`
  screenshot of the same moment looked normal. Fix: `Mod4+F10` (`dotfiles/sway/screen-reset.sh`:
  output power off, 2 s, power on); `swaymsg 'output DSI-1 power off'` then `power on` does the same
  remotely. Root cause unknown (panel/DSI resync after a mode or buffer change). Not specific to
  Wine/DXVK/Vulkan: native SDL games do it too (DevilutionX: SDL2, X11 via Xwayland, OpenGL/GLX).
  - What it is (2026-10-09 session): the kernel's view is identical in good and bad state (same
    plane, fb 140 XR24 X-tiled, rotation 0, no kernel messages, `grim` fine). The only difference
    found is the DSI controller's `MIPI_INTR_STAT` (`0x18b804`) with bit 20 `DPI_FIFO_UNDERRUN` set
    (0x40300000 vs 0x40200000); a panel power cycle clears it (modeset). The bit is a sticky latch,
    so it doesn't say "bad right now". Other differing registers (`0x1e1708`, `0x1e170c`, ...) are
    noisy, not indicators. Correlated with heavy GPU load (Sam & Max first room ~20 fps; worse with
    CPU turbo off). Underrun log at 10:41, 10:50, 10:57 matched glitch sessions; none in 17 clean min.
  - Ruled out (tested): direct scanout (every fb on the plane is allocated by sway), GPU runtime
    PM / RC6 (held awake with `i915_forcewake_user`), CPU deep C-states (`/dev/cpu_dma_latency` 0),
    GPU frequency changes (pinned 400 MHz via `rps_min_freq_mhz`), the game's own mode switching
    (Wine virtual desktop), GPU hangs/resets (`i915_wedged` 0, error state empty), `max_render_time`
    (inconclusive), FBC/PSR/DMC (not present on this chip/DSI). Not tested: memory pressure/bandwidth
    as the cause, kernel DSI tracing, a different kernel.
  - Register poking notes (2026-10-09): `intel_reg` needs a read-write mapping of
    `/sys/bus/pci/devices/0000:00:02.0/resource0` (keep it `0600`; a `0440` rule breaks it even for root).
    Display registers read `0xffffffff` (and the kernel logs "Invalid mmio detected during user access")
    while the output is powered off. DSI port C control, by the driver headers (`vlv_dsi_regs.h`,
    `_MIPI_PORT(port, a, c)`), is `0x1e1700` (VLV display base `0x180000` + `0x61700`) but it reads 0
    in every state although the panel works: known hardware quirk, DPI_ENABLE never reads back on VLV/CHV port C
    (driver comment in `vlv_dsi.c` `get_hw_state`; audit 2026-10-09). `0x1e1708`/`0x1e170c` change between reads and
    are not defined anywhere in i915. Do NOT write DSI registers (e.g. a `DPI_ENABLE` toggle to
    test "host vs panel") until the real control register is identified. Driver sequence for reference:
    enable = DPI `TURN_ON` command, 100 ms, panel DISPLAY_ON, then `DPI_ENABLE` in the port control;
    disable = `SHUTDOWN` command, port control `DPI_ENABLE` cleared.
  - **REFUTED 2026-10-09 15:39 for the desync:** a persistent desync happened in the fresh-boot state, with
    `0x18b810` = `0x3fffff` (read-only logger: unchanged since boot, no modeset), so the truncated
    timeout below is NOT what makes the picture desync. Underruns/flashes also occur with the long value (15:31:43).
    The `u16` truncation is still a real driver bug (worth reporting upstream) but apparently harmless here.
    Still unknown what makes some underruns persistent. Everything below up to "Live register writes" is the
    original (wrong) theory, kept for the facts (VBT decode, register arithmetic).
  - **Probable root cause found in the driver (2026-10-09), untested:** `vlv_dsi.c` programs
    `MIPI_HS_TX_TIMEOUT` (0x18b810, "recovery" timer: one frame in byte clocks for non-burst video mode)
    with `txbyteclkhs(vtotal * htotal, ...)`, whose pixel argument AND return value are `u16`.
    774 x 1308 = 1,012,392 px wraps to 29,352, so the register is 22,015 (`0x55ff`, exactly what the
    hardware shows) instead of ~759,295 (`0xb95ff`): a ~0.5 ms timeout instead of one 16 ms frame
    (upstream master still has the `u16`). The VBT (`i915_vbt` in debugfs, decode with `intel_vbt_decode`)
    says: MIPI port C, 4 lanes, RGB888, **non-burst with sync events** (no slack), clock stop off, EOT on,
    HSTxTimeOut 0x3fffff (ignored for non-burst), panel init = Jadard-type (E1/E2/E3 unlock, sleep out,
    lane setting 0x03, display on), reset on GPIO 72, backlight on GPIO 70. The comment in the driver says
    that when the counter expires the controller ends the HS transmission (EOT, stop state), so with the
    truncated value this happens many times per frame (consistent with `MIPI_INTR_STAT` bit 21
    HS_TX_TIMEOUT always set); each forced restart leaves a gap that non-burst mode cannot absorb, the DPI
    pixel FIFO underruns (bit 20) under load, and a stop/restart can leave the stream misaligned.
    **Live register writes are NOT safe (tried 2026-10-09, DON'T repeat):** writing `0x18b810` = `0xb95ff` and
    then clearing the latch/bit 21 in `0x18b804` on the running system was followed within a minute by
    `[CRTC:86:pipe B] flip_done timed out` (pipe stalled) and a hard lock (black screen) when suspend started; only a
    hard reset recovered. Also: a **fresh boot has `0x18b810` = `0x3fffff`** (the VBT value, left by the firmware: no
    DSI modeset has run yet); the truncated `0x55ff` appears only after the driver's first modeset (screen
    power off/on, resume, Mod4+F10, a mode change). Read-only check of the theory: does the glitch rate differ
    between the fresh-boot state and after the first modeset? (`~/.cache/gpd/glitchlog/watch.py` logs the register).
    The reliable test is the patched driver (below), loaded through a reboot with a way back.
    Fix: `kernel/i915/0001-*.patch` (u32 + 64-bit multiply), to send upstream (draft, not sent).
  - **Driver audit (2026-10-09, desktop, `kernel/dsi-investigation/FINDINGS.md`)**: every DSI register equals the driver's
    arithmetic and the GOP programs the same values at fresh boot (only HS_TX_TIMEOUT differs). The pipe's own FIFO
    never underruns (reporting is on in every state, no message): only the DSI controller's DPI FIFO does, so the
    problem sits between pipe output and the DSI serializer, not memory bandwidth/watermarks (cleared). Port C control
    reading 0 is a known hardware quirk (DPI_ENABLE never reads back on VLV/CHV port C), not a mapping error.
    cdclk is **266667 kHz** (fresh boot and after modesets). Upstream fixed the same symptom on sibling chips twice:
    `c8dae55a8ced` (Bay Trail DSI: picture shifted with wraparound and wrong colors at cdclk 266667, needs >= 320000;
    applied to Valleyview only, not Cherry Trail) and `f90e8c36c886` (Broxton split screen with cycled colors: DPI FIFO
    not flushed at frame end, `EOT_DISABLE` bit 9). Neither is proven for CHV; the cdclk one was tested (patch 0002) and showed no improvement.
  - **i915 patch tests** (`kernel/i915/`): `build.sh` on the desktop builds a module for exactly 7.2.9-arch1-1;
    `install-test.sh i915-XXXX.ko.zst` on the GPD makes `/boot/initramfs-linux-i915test.img` + entry
    `arch-i915test.conf` and sets it as one-shot for the next boot (5 s menu); the stock entry stays default, so a
    black screen is fixed by a hard reset. Check after boot: `cat /sys/module/i915/srcversion`, cdclk in
    `/sys/kernel/debug/dri/1/i915_cdclk_info`. Boot is a fastset (GOP state kept): a patch only takes effect after the
    first driver modeset, so do one panel reset (Mod4+F10) after boot before testing, with stock too. Keep it: `sudo bootctl set-default arch-i915test.conf`; remove:
    `uninstall-test.sh`. A kernel update makes the test entry useless (module is for 7.2.9-arch1-1 only); remove it.
    Test: play the scenes that glitch (DevilutionX, Zelda 3, Sam & Max) with
    `sudo python3 kernel/dsi-investigation/data/watch-readonly-logger.py LOG STOPFILE`; compare time to the first
    underrun latch after a panel reset (stock: ~1.5-2 min) over several runs, and whether a persistent desync happens.
    | Date | Module | Result |
    |---|---|---|
    | 2026-10-09 | 0002 only (CHV cdclk >= 320000, srcversion 128FFB8FD09197039ED50CD) | Booted fine (fastset keeps GOP cdclk 266667; one panel reset -> 320000, as intended). DevilutionX: underrun latch + a momentary flash 34 s after the reset (16:21:32): **cdclk 320 MHz does not stop the underruns/flashes.** Many more momentary flashes in ~15 min of play; at ~16:35 a split (cdclk verified 320000) that held for several seconds and then recovered on its own; stock splits also sometimes recovered by themselves. **No improvement seen: cdclk is not the cause** (only open: whether never-recovering splits get rarer, not measurable well). Another (small-shift) split at 16:38; register snapshots of both: `kernel/dsi-investigation/data/split-0002-regs.txt` (identical to the stock bad state). |
    | 2026-10-09 | 0003 only (CHV `EOT_DISABLE` bit 9 = BXT DPI FIFO flush, srcversion 251C1EC9D5CBEFF7D7D8A09) | Booted fine; after the panel reset `0x18b85c` reads **0** although the driver wrote bit 9 (and 0x55ff shows the modeset ran): port C does not keep the bit on CHV. Test void (module = stock). |
  - Automatic reset tried and REMOVED (2026-10-09): `inputd` power-cycled the output once a second-check
    saw the underrun bit. It fired 9 times in a day, including during Commander Keen with nothing wrong on
    screen: the bit is a sticky latch that is set by harmless underruns too, it does not mean "picture
    broken now". Do not rebuild this on that bit. Manual reset stays (`Mod4+F10`).
    An automatic detector would need a real "picture is wrong" signal (none found: kernel state, plane
    registers and screenshots all look normal in the bad state).

## Prior art: ViccRondo/gpd-win1-atomic-gaming (checked 2026-10-07, commit 325f405)
An atomic Fedora/Bazzite-style GPD Win 1 image (KWin + Steam Gamepad UI). Useful findings:
- Confirms our xHCI fix: they also disable wakeup on `0000:00:14.0` because of EBUSY in s2idle,
  and additionally on the internal keyboard device `0603:0002`.
- Confirms Vulkan 1.2 on this GPU; some old games run with Proton 7 (DXVK 1.10.x era).
- Their known issue: long/repeated suspend cycles can leave the i915 DSI display black or freeze
  the machine. Kernel patch `drm/i915/chv: Retry stalled DSI transcoder enable` (resume leaves
  the transcoder "enabled" but the scanline counter stopped → `flip_done timed out`), plus a
  watcher (`win1-i915-watch`) that saves i915 debugfs state and force-reboots on
  `flip_done timed out` / `commit wait timed out` / `vblank wait timed out on crtc`.
  Our journal had none of these messages across 11 boots (2026-10-07). If they appear: their
  patch is the lead (needs a custom kernel), and copying their diagnostics capture (without the
  forced reboot) would help.
- Kernel args they set without documented reasons: `reboot=pci` (reboot hangs on some GPDs) and
  `i915.disable_power_well=0` (keeps display power wells on; commonly used against DSI
  black-screen-after-resume, costs some idle power). Candidates only if we see those problems.
- Lid wake race: after an open-lid resume, a spurious lid-close event can immediately re-suspend
  the device; they ignore lid-close events within 8 s of a resume (`win1-lid-event-guard`).
  Watch for "goes back to sleep right after waking".
- Coredumps disabled (`Storage=none`) to save eMMC space and time: adopted here too
  (`/etc/systemd/coredump.conf.d/90-gpd.conf`).
- The in-session "split picture" (bottom half on top) that we fix with `Mod4+F10` is not covered
  by their patch (different symptom: ours keeps running, theirs stalls).

## Memory map (checked 2026-10-07)
- 4 GB physical, 3.7 GB `MemTotal`. Firmware keeps ~94 MB: 32 MB graphics stolen memory (DVMT
  pre-allocated; already minimal), an 18 MB block at 0x1f000000, ACPI/EFI tables. The kernel
  keeps the rest: page bookkeeping (~64 MB), kernel image, and a 64 MB swiotlb bounce buffer
  (RAM extends above 4 GB; peak use seen 1.6 MB, so `swiotlb=8192` would give back 48 MB if RAM
  ever gets tight).
- The BIOS "512 MB" graphics setting is the GPU aperture (PCI BAR at 0x80000000): address space
  for CPU access to GPU memory, it costs no RAM. Leave it. GPU buffers come from normal RAM on
  demand.

## Analog sticks (measured 2026-10-08)
- The sticks snap to the axes, from the controller firmware: at half deflection only ~14% of
  samples fell in the diagonal sector (a round stick gives ~41%), i.e. a per-axis dead zone; at
  full deflection the range is square (diagonals reach both maxima). The kernel adds almost
  nothing (xpad `flat 128`, `fuzz 16` of +-32768).
- No firmware update path known for the Win 1 (GPD's tools are Windows-only and for later
  models). A uinput remapper could only smooth the jump at the threshold; decided to leave it.

## Audio after hibernation (2026-10-08, solved 2026-10-09)
- After waking from hibernation the rt5645 codec reported headphones (and mic) plugged in with
  nothing in the jack, the speaker was silent and the headphones gave static. Cause and fix: below.
  What did NOT work: a real s2idle suspend; rebinding the codec or reloading the sound modules on a
  running system (hangs the kernel, `rmmod` stuck in D state until reboot); plugging a jack in and
  out (only helped when the driver's jack interrupt happened to be on).

### Audio broken after hibernate: codec not restored (kernel driver bug, 2026-10-09)
- Symptoms after any hibernate: speaker silent although the whole path is powered and unmuted,
  headphones give static instead of sound, plugging/unplugging does nothing (the `chtrt5645 Headset`
  input switch stays "headphone+mic inserted"). `scripts/audio-speaker` only fixes routing; a
  reboot, or reloading the sound card (below), fixes it. Not an aborted hibernate: the machine
  really powers off and resumes (the `acpi_resume_power_resources` / `P18W._ON` warning and "i2c
  Transfer while suspended" in the log are a separate, harmless resume-path error: P18W is a PMIC
  1.8 V rail for the SD controller/cameras, not the codec; the codec's only power resource is CLK3).
- Root cause, shown with a trace of the whole hibernate (events `power:device_pm_callback_*` and
  `regmap:*` filtered to the codec, see below): the codec loses power; on `[restore]` the driver
  (`rt5645_sys_resume`) runs `regcache_sync()`, which writes ~30 registers but skips (a) registers
  whose cached value equals the reset default in its defaults table (it assumes a freshly reset
  chip), and (b) volatile registers, which are not in the cache at all. Probe does a one-time setup
  that falls in exactly those groups and is never redone: `init_list` (private registers
  `PR_BASE+0x3d/0x1c/0x20/0x21/0x23`, the amp/analog settings, plus `ASRC_4` `0x8a` = `0x0120`,
  which the cache already holds as its default so the sync skips it while the chip powers up with 0)
  and the jack detection setup (`IRQ_CTRL2` `0xbd`, `A_JD_CTRL1`). Consequences: static through the
  headphones, a silent speaker, and with `0xbd` = 0 no jack interrupt ever fires, so the jack state
  is frozen at "inserted".
  Evidence: codec registers via `/sys/kernel/debug/regmap/i2c-10EC5645:00-nocache/registers`
  (`0bd: 0000`, `08a: 0000` after, `0280` / `0120` before) and `scripts/audio-snapshot`.
- Second bug, in the sound DSP driver (Intel SST, `snd_intel_sst_core`): `intel_sst_pm` only has
  `.suspend/.resume/.runtime_suspend`, so hibernate's `.freeze/.thaw/.poweroff/.restore` do nothing
  for it (trace: `[freeze]`/`[restore]` return at once). The DSP firmware is neither saved nor
  restored; when the DSP was not runtime-suspended at hibernate time the driver believes the
  firmware runs after the power loss and every IPC times out (`sst: Busy wait failed`, `Wait timed-out
  condition:0x0, msg_id:0x1 fw_state 0x3`, `fw returned err -16`, `ASoC: PRE_PMD: pcm0_in event
  failed: -16`), `pw-play` hangs, nothing sounds. Idle-DSP hibernates happen to work (state RESET
  -> firmware reloaded on first use), which is why the static-noise case was seen first.
  Patch: `kernel/intel-sst/` (9 changed lines of `sst.c`: map the four callbacks to the existing
  suspend/resume handlers) (built by `kernel/prepare.sh`).
- Workaround that works without a reboot: stop PipeWire/WirePlumber, `modprobe -r
  snd_soc_sst_cht_bsw_rt5645` (also unloads the codec), `modprobe snd_soc_sst_cht_bsw_rt5645`, start
  PipeWire (re-probes the codec; stop PipeWire first, unbinding with the card open hung the kernel once).
  Writing only the lost registers (`0xbd`, `0x8a`, `0x83`, `0xf8`) with `i2ctransfer -f -y 1 w3@0x1a REG HI LO`
  fixed the jack but not the sound (private registers can't be read back).
- Live module swapping is not safe (2026-10-09): replacing `snd_soc_rt5645` by `insmod` of the patched
  module while the machine was running hung twice. Unloading the codec waits in `rt5645_i2c_remove` ->
  `cancel_delayed_work_sync(jack_detect_work)` forever (`rmmod` in D state), after the kernel oops'd in
  `rt5645_jack_detect_work` (page fault in `mutex_lock`, the DSP was wedged and the work hit
  `PRE_PMD ... event failed: -16` errors). Only a reboot clears it. So test the patches by installing them
  in `/usr/lib/modules/$(uname -r)/updates/` (or DKMS) and rebooting.
- Proper fix: `kernel/rt5645/` (patch against the 7.2.9 `sound/soc/codecs/rt5645.c`):
  a `.restore` PM handler that soft-resets the codec, redoes the one-time setup of probe (now
  `rt5645_hw_init()`, used by probe and restore) and then resyncs the cache. v1 only redid the jack
  setup: the jack worked after a real hibernate (trace: `0xbd` rewritten) but `0x8a` and the private
  registers were still lost, hence v2. `kernel/prepare.sh` builds the tree. Status: installed via DKMS and proven
  (2026-10-09): after a real hibernate/resume with the DSP active (tone playing just before) the codec registers
  are back (`0xbd` = `0x0280`, `0x8a` = `0x0120`), no SST errors, sound plays, the jack state is right, no
  reload needed; worth sending upstream (alsa-devel; both patches carry commit messages) (alsa-devel, Realtek
  rt5645 maintainers) once it is proven.
- Installing the patches: `kernel/install.sh` (sudo): `kernel/prepare.sh` assembles
  `kernel/dkms-tree/gpd-audio-1.0/` (pinned upstream 7.2.9 sources of `rt5645.c` and the Intel atom
  SST driver, sha256-checked, our two patches applied, Kbuild Makefiles, `dkms.conf`), which is copied
  to `/usr/src/gpd-audio-1.0` and installed with DKMS (`dkms` + `linux-headers`, official repo) into
  `/usr/lib/modules/<kernel>/updates/` (depmod prefers it: search order `updates extramodules
  built-in`). The dkms pacman hook rebuilds it on every kernel update, so no manual step; if a build
  fails the stock modules stay (sound works, the hibernate bug is back). Then reboot (never
  load/unload the sound modules live). `kernel/uninstall.sh` reverts. The sources stay the 7.2.9 ones
  the patches were written for, compiled against whichever kernel is installed; a kernel API change
  can break the build (fail-safe), then the pins/patches need updating. Best long-term: upstream.
- Reproduce and trace: `sudo systemctl hibernate`, power on, compare `scripts/audio-snapshot`
  dirs. Tracing: `/sys/kernel/tracing/events/regmap/{regmap_reg_write,regcache_sync,regmap_cache_only}`
  with filter `name == "i2c-10EC5645:00"` and `events/power/device_pm_callback_{start,end}` with
  `device ~ "*10EC5645*" || device ~ "*808622A8*" || device ~ "*cht-bsw*"`.
- Loudness check without ears: play a 440 Hz tone at 100% and record the internal mic
  (`pw-record --target alsa_input.platform-cht-bsw-rt5645.HiFi__Mic__source`): -31.0 dBFS on 2026-10-09
  after a card reload (compare after changes). The reported volume looked "low" after the reload; all
  codec/DSP gain registers equalled the pre-hibernate ones, cause not found.

### Quick fix: `scripts/audio-speaker` (2026-10-09)
- Recurred on 2026-10-09 with no plug/unplug by anyone: stale "headphones plugged in" since the
  hibernate attempt at 22:42 the evening before (the hibernate aborted and the system resumed).
- Try first: `scripts/audio-speaker`. It selects the card profile `HiFi (Mic, Speaker)` (the
  profile list is not filtered by jack state) and makes the speaker sink the default. Same as
  `pactl set-card-profile alsa_card.platform-cht-bsw-rt5645 "HiFi (Mic, Speaker)"`. Sound came
  back immediately. `scripts/audio-speaker auto` restores the normal profile (use after really
  plugging in headphones). Lasts until reboot/next profile reset.
- Diagnosis (what does NOT work): a real s2idle suspend (`rtcwake -m freeze -s 10`) does not clear
  it; the driver re-runs jack detect on resume and still gets "jack in". The codec's own status
  register says plugged: `INT_IRQ_ST` (0xbf) = 0x0880 with bit 0x1000 clear = jack in; `IRQ_CTRL2`
  (0xbd) reads 0. Read with `sudo grep -E '^0(bd|bf):' /sys/kernel/debug/regmap/i2c-10EC5645:00-nocache/registers`.
  Input switch state: `chtrt5645 Headset` (event18) SW bits 2 and 4 set (headphone+mic insert).
  IRQ injection is unavailable (no /sys/kernel/debug/irq); there is no userspace way to re-trigger
  the jack work except the unsafe codec unbind. Root cause unknown (chip vs stuck jack contact).
