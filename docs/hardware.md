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
- A game's fullscreen switch once left the picture split (bottom half on top). Fixed by
  `swaymsg output DSI-1 power off; ... power on`.
