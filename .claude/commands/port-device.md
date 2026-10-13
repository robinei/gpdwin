---
description: Add a new device to the repo (devices/<name>/) - argument: ssh target of the device running Arch (installed or the install medium)
---

Make this repo work on a device it doesn't know yet (`$ARGUMENTS`), by writing its device layer, not by
changing the shared code for it. Read docs/repo-layout.md first; `devices/gpd-win1/` is the worked example.

1. **Survey** (read-only): DMI (`/sys/class/dmi/id/*`), `lscpu`, `lspci -nn`, `lsusb`, input devices
   (`/proc/bus/input/devices`: names, which has the buttons), outputs and modes (`/sys/class/drm/*/modes`,
   rotation: a portrait panel in a landscape device), power supplies (`/sys/class/power_supply/*/type`,
   battery and charger names, `charge_control_end_threshold` or a charge voltage), CPU power and turbo
   controls (intel_pstate / RAPL, or amd-pstate / `cpufreq/boost`, asus-wmi or similar `ppt_*`), hwmon
   names (`coretemp`, `k10temp`), the journal of a boot for errors (`journalctl -b -p warning`).
   **Charging**: the maker's specified charger (voltage, current) against what Linux negotiates
   (`/sys/class/power_supply/*` of type USB / `tcpm-source-psy-*` `voltage_now`, the driver's sink PDOs). If
   Linux asks USB-PD chargers for more than the maker specifies, make it ask for the specified voltage
   (a DKMS module like `devices/gpd-win1/kernel/usbpd/`) or document why not.
2. **Research**: the Arch wiki page for the model, kernel quirk tables (`drm_panel_orientation_quirks.c`,
   `hid-*`), known suspend/audio/firmware problems. Note sources in the device docs.
3. **Write `devices/<name>/`**:
   - `README.md`: how to recognise it, what works, what this layer fixes, known problems (the GPD's as template).
   - `device.conf`: only keys whose auto-detection is wrong or that have no default (src/devconf.h,
     docs/repo-layout.md "device.conf"): `power_default_w`, `power_steps_w`, `menu_key` (a button that should
     open shelf; with no keyboard, a pad or side button), `pad_name`, `charger` if more than one supply has
     `online`, ...
   - `manifest.tsv` + files: boot entry (`system/boot/loader/entries/arch.conf` with `root=UUID=@ROOT_UUID@`),
     `mkinitcpio.conf`, udev rules, sleep hooks, a sway snippet (`dotfiles/sway.conf` linked to
     `~/.config/gpd/sway.d/10-device.conf`: output name, rotation, scale).
   - `packages.txt` (kernel, firmware, microcode, drivers), `services.txt`.
   - `docs/` for findings as they come (measured values, what didn't work).
4. **Shared code only when it is generic**: if a program can't express this device through device.conf
   (e.g. a different GPU's clock controls), extend the program for the new kind of hardware with
   auto-detection, keep the GPD Win's behaviour unchanged, and say in the commit which devices it was tested on.
5. Install with `/install` (or `scripts/sync install` on an existing Arch), check suspend, hibernate,
   sound, brightness, battery, the pad in shelf, idle. Record results and commit.
