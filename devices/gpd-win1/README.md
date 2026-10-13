# GPD Win 1 (2016, Atom x7-Z8700)

Device layer for the original GPD Win: a 5.5" Cherry Trail handheld with a keyboard. Everything here is
specific to this hardware; the rest of the repo is shared. Paths inside `kernel/`, `mesa/` and `dxvk/` are
written relative to this folder.

**How to recognise it** (its firmware reports "Default string" for most DMI fields, so check the parts):
Intel Atom x7-Z8700, 4 GB, Broadcom BCM4356 Wi-Fi (`brcmfmac4356-pcie.gpd-win-pocket.txt`), Realtek RT5645
audio, a 720x1280 DSI panel (`DSI-1`), gamepad `Microsoft X-Box 360 pad` (045e:028e) when its switch is
in gamepad mode, fuel gauge `max170xx_battery`, charger `bq24190-charger`. BIOS: American Megatrends,
2016-10-18 or similar.

## What is fixed or improved here

Measured on one device; details and evidence in `docs/` (hardware.md, power.md, boot.md).

| Problem on stock Arch | What this layer does |
|---|---|
| No backlight control (`Failed to get the SoC PWM chip`) | loads the LPSS PWM driver before i915 (`mkinitcpio.conf`) |
| Portrait panel | console and sway rotated (`fbcon=rotate:1`, `output DSI-1 transform 90`) |
| Suspend aborts (`xhci_hcd ... returns -16`) | USB3 controller wakeup off (`90-xhci-no-wakeup.rules`) |
| Backlight at minimum after hibernate | saved and restored around sleep (`system-sleep/backlight`) |
| Charging stuck at 500 mA after hibernate | the PMIC's charger registers restored (`system-sleep/pmic-charger`) |
| No sound after hibernate (codec not restored) | patched `rt5645` and SST modules via DKMS (`kernel/`, package `gpd-audio`) |
| Picture splits (bottom half on top) under GPU load | patched i915 that resyncs on a DPI underrun (`kernel/i915/`, DKMS `gpd-i915`); Super+F10 by hand |
| Display flashes with OpenGL games; slow GL start | own Mesa build (`mesa/`): crocus upload/readback fixes, no LLVM, ~40 % faster GL start |
| Vulkan stuck at 1.2, slow depth | the same Mesa: hasvk with real Vulkan 1.3 and HiZ restored (up to 5x in fill tests) |
| DXVK stalls in Dark Souls (readback every frame) | patched DXVK d3d9 (`dxvk/`, `DXVK_GPD_ASYNC_READBACK=1`) |
| Quiet, distorting speakers | speaker-only compressor in PipeWire (`dotfiles/pipewire`, tuned with Super+C) |
| Battery charged to 4.38 V (high-voltage cell) | charge limit 4.144 V, ~80 % (`charge-limit`; shelf Utilities toggles it) |
| Firmware power limit 6 W | per-game limit 1-8 W in shelf (W key); see the board fault note below |
| Bluetooth stack loads for a radio without firmware | blacklisted (`no-bluetooth.conf`) |
| Lid wakes the device from sleep | `gpiolib_acpi.ignore_wake=INT33FF:00@35` |
| ~25 s from power button to frontend | ~22 s: silent boot, getty without the idle wait, external ports off, GPU driver prefetch |

## Installing the parts that don't come from pacman
- **Kernel modules** (sound after hibernate; split-screen resync): `kernel/install.sh` and
  `kernel/i915/install-dkms.sh` on the device (DKMS rebuilds them for each kernel; the i915 one takes ~10 min on
  the Atom). Without them the device works, minus those two fixes. The boot menu (hold Space) has a stock entry.
- **Mesa**: build on a faster x86_64 Arch machine with `mesa/build.sh` and `mesa/build.sh lib32` (needs `devtools`
  and sudo there), copy the packages over, `mesa/install.sh` on the device. `mesa/rollback.sh` returns to Arch's.
- **DXVK**: `dxvk/build.sh` on the build machine; per game, see `.claude/commands/setup-game.md`.

## Known problems
- **2026-10-13: Robin's unit has a failed keyboard controller (SH61F83Q) drawing ~3 W and smelling burnt**
  (docs/power.md, "Overheating"). It followed days of running at 8 W with turbo and charging at 9-12 V USB-PD.
  Possibly ageing alone, but until more is known, prefer the firmware's 6 W and a 5 V charger on this device.
- Display flashes are much rarer with the Mesa fixes but not gone (docs/hardware.md "Display").
