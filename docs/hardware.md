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
  - Workaround, automatic: `inputd` power-cycles the output when the bit sets (docs/desktop.md).
    Auto-resets are logged in `~/.cache/gpd/dsi-resets.log`: look there to see how often it happens.

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

## Audio jack after hibernation (2026-10-08)
- After waking from hibernation the rt5645 codec reported headphones (and mic) plugged in with
  nothing in the jack: UCM switched the speaker off, PipeWire showed only "Headphones", no sound.
  Rebinding only the machine driver didn't help. Rebinding the codec (i2c `i2c-10EC5645:00`) and
  then the machine driver (`cht-bsw-rt5645`) fixed it once, but the second time the codec unbind
  hung in the kernel (D state in `rt5645_i2c_remove` -> `cancel_delayed_work_sync`, waiting for
  its jack-detect work), leaving audio dead until reboot. Do NOT automate that in a sleep hook
  (a hang there could block resume; the hook was added and removed again the same day).
  Safe manual fix: plug something into the jack and pull it out, or reboot.

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
  chip, but the power-on value of e.g. `0x8a` ASRC_4 is 0, the table says `0x0120`: ASRC stays off,
  so the DAC clock does not match the 48 kHz I2S stream: static), and (b) volatile registers, which
  are not cached at all: `IRQ_CTRL2` (`0xbd`, the jack-detect interrupt enable, set once in probe)
  and `A_JD_CTRL1`. With `0xbd` = 0 no jack interrupt ever fires, so the jack state is frozen.
  Evidence: codec registers via `/sys/kernel/debug/regmap/i2c-10EC5645:00-nocache/registers`
  (`0bd: 0000`, `08a: 0000` after, `0280` / `0120` before) and `scripts/audio-snapshot`.
- Workaround that works without a reboot: stop PipeWire/WirePlumber, `modprobe -r
  snd_soc_sst_cht_bsw_rt5645` (also unloads the codec), `modprobe snd_soc_sst_cht_bsw_rt5645`, start
  PipeWire (re-probes the codec; stop PipeWire first, unbinding with the card open hung the kernel once).
  Writing only the lost registers (`0xbd`, `0x8a`, `0x83`, `0xf8`) with `i2ctransfer -f -y 1 w3@0x1a REG HI LO`
  fixed the jack but not the sound (private registers can't be read back).
- Proper fix: `kernel/rt5645/` (patch against the 7.2.9 `sound/soc/codecs/rt5645.c` + `build.sh`):
  a `.restore` PM handler that soft-resets the codec, resyncs the cache and redoes the jack setup
  (`rt5645_jd_init()`, factored out of probe). `kernel/rt5645/build.sh` builds `build/snd-soc-rt5645.ko`
  against `linux-headers` (does not load anything). Status: builds; NOT yet loaded/tested (loading a
  self-built module as root needs the user's go-ahead); worth sending upstream (alsa-devel, Realtek
  rt5645 maintainers) once it is proven.
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
