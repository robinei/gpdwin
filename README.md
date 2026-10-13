# Handheld Arch

Fixes and an optional game-console setup for Arch Linux on handheld gaming PCs, applied on top of a
standard Arch install. Supported device: **GPD Win 1** (2016, Intel Atom x7-Z8700). Other devices can be
added as a device layer (see "Adding a device").

There are three levels; take only the first if that is all you want:

1. **Device fixes.** Kernel modules, boot and initramfs settings, udev rules, sleep hooks, a charge limit,
   patched Mesa. They work under any desktop or none. Host file `components=none` (see "How to use it"),
   or copy the fixes by hand from the device folder.
2. **The handheld session**, on top of the fixes: sway with swaybar and our status line, **inputd** (idle,
   lid and power-button policy) and **shelf** (a game frontend). The device boots straight to the game list,
   and per-game power and turbo settings are applied at launch. This level is built for sway.
3. **Extras**, all optional: a game installer (Steam and DRM-free), save backup and sync with a desktop PC,
   and agent playbooks for installs and maintenance.

![shelf on the GPD Win 1: collection tabs, the game list with box art, details of the selected game, and the
status bar above](docs/images/shelf.png)

*shelf on the GPD Win 1 (1280x720), with the status bar above it: CPU and GPU clock, power limit, temperature,
RAM, Wi-Fi, volume, battery.*

## GPD Win 1: what this fixes

Problems on a stock Arch install, and what the repo does about them. Measured on one unit; details in
[devices/gpd-win1](devices/gpd-win1/README.md) and its `docs/`.

| Problem on stock Arch | Fix |
|---|---|
| No backlight control (`Failed to get the SoC PWM chip`) | LPSS PWM driver loaded before i915 (`mkinitcpio.conf`) |
| Portrait panel | console and sway rotated (`fbcon=rotate:1`, `output DSI-1 transform 90`) |
| Suspend aborts (`xhci_hcd ... returns -16`) | USB3 controller wakeup off (udev rule) |
| Lid wakes the device from sleep | `gpiolib_acpi.ignore_wake=INT33FF:00@35` |
| No sound after hibernate: speaker silent, headphones static, jack dead | kernel patches to `rt5645` and the Atom SST driver (DKMS `gpd-audio`) |
| Charging stuck at 500 mA after hibernate | the PMIC's charger registers restored (sleep hook) |
| Backlight at minimum after hibernate | saved and restored around sleep |
| USB-PD chargers negotiate 9-12 V; GPD specifies 5 V | patched `chtwc_int33fe` that asks for 5 V only (DKMS `gpd-usbpd`) |
| Battery charged to 4.38 V | charge limit 4.144 V (~80 %), toggled in shelf |
| Firmware power limit fixed at 6 W | per-game limit 1-8 W and turbo on/off in shelf |
| Picture splits (bottom half on top) or flashes under GPU load | Mesa patches remove the trigger; patched i915 resyncs the panel (experimental); Super+F10 by hand |
| Display frozen at screen-on after a long screen-off (vblank and flip timeouts) | i915 patch for a lost interrupt-enable race (in the experimental i915 build) |
| Vulkan 1.2 only; no depth HiZ on hasvk since Mesa 23.0 | patched Mesa: hasvk reports Vulkan 1.3, HiZ restored (depth fill up to 5x faster, DXVK D3D9 test 35 -> 182 fps) |
| Slow OpenGL texture uploads (uncached CPU copies) | patched crocus: uploads 3.7 -> 1.1 ms; build without LLVM, ~40 % faster GL start |
| 32-bit DXVK >= 2.5 broken on hasvk; Dark Souls stalls on a readback every frame | DXVK 2.4.1 with a d3d9 patch: Dark Souls 19-24 -> 30 fps (its cap) |
| Quiet, distorting speakers | speaker-only compressor in PipeWire |
| Bluetooth stack loads for a radio without firmware | blacklisted |
| ~25 s from power button to the game list with shelf, about 3 s more with Pegasus Frontend (estimated, not timed) | ~22 s: boot tweaks; shelf shows its first frame ~40 ms after it starts |

### Driver patches
None are submitted upstream yet. Each patch's message explains the bug and how it was found.

| Patch | Driver | What it does |
|---|---|---|
| [rt5645 0001](devices/gpd-win1/kernel/rt5645/) | ASoC rt5645 codec | restores the codec's private and jack-detect registers after hibernation |
| [intel-sst 0001](devices/gpd-win1/kernel/intel-sst/) | ASoC Atom SST | hibernation PM callbacks: save and restore the DSP firmware |
| [usbpd 0001](devices/gpd-win1/kernel/usbpd/) | `intel_chtwc_int33fe` | GPD Win sink PDO 5 V / 2.5 A only (kept local: the DMI match also covers the GPD Pocket, which ships a 12 V charger) |
| [i915 0001](devices/gpd-win1/kernel/i915/) | i915 DSI | u16 overflow of `MIPI_HS_TX_TIMEOUT` on VLV/CHV (any panel over 65535 pixels per frame) |
| [i915 0015](devices/gpd-win1/kernel/i915/) | i915 VLV/CHV irq | `VLV_IER` lost to a racing irq handler: display interrupts off until the next power-well cycle |
| [i915 0002-0014, 0016-0019](devices/gpd-win1/kernel/i915/) | i915 DSI | experiments and diagnostics for the DPI FIFO underruns behind the split picture; the DKMS build `gpd-i915` applies 0001 and 0011-0017 (0012: resync after a lone underrun) |
| [mesa 0001-0005](devices/gpd-win1/mesa/) | crocus (Gfx7/8 OpenGL) | no snooping for staging textures, GPU blits for large tiled uploads and readbacks on non-LLC parts |
| [mesa 0006](devices/gpd-win1/mesa/) | hasvk (Gfx7/8 Vulkan) | restores HiZ, dropped by accident in Mesa 23.0 |
| [mesa 0007-0011](devices/gpd-win1/mesa/) | hasvk | Vulkan 1.3 on Cherry Trail: softpin with a 32-bit layout, 256-byte push constants, `VK_KHR_pipeline_library`, no spin on a full state pool |
| [dxvk 0001](devices/gpd-win1/dxvk/) | DXVK d3d9 | optional async readback of small SYSTEMMEM images; a log of GPU waits |

The audio and USB-PD modules install with `scripts/sync install` (DKMS). The i915, Mesa and DXVK builds are
separate steps ([devices/gpd-win1](devices/gpd-win1/README.md), "Installing the parts that don't come from
pacman").

## Contents

The parts in more detail.

**Device fixes**: one folder per device, listing the problems on stock Arch, the fix and the measurements
(above for the GPD Win 1).

**shelf** (`src/shelf.c`), a game frontend:
- one C file using SDL3, native Wayland; no sway-specific code, but only tested under sway (sway rules put
  games fullscreen on top of it);
- reads gamepads through evdev;
- library in Pegasus Frontend's format (`metadata.pegasus.txt`, `game_dirs.txt`, `media/`, `stats.db`), so
  Pegasus collections, including those EmuDeck generates for Pegasus, can be used unchanged
  ([docs/pegasus-format.md](docs/pegasus-format.md));
- per-game settings for CPU/GPU turbo, a package power limit and a frame-rate cap; play time and favourites.

**inputd** (`src/inputd.c`): idle policy (dim, screen off, suspend), lid, power button, and a key that opens
shelf. Gamepad input counts as activity. Uses sway's IPC, so it needs sway.

**statusbar** (`src/statusbar.c`): status line for swaybar (i3bar protocol) (CPU/GPU clock, power limit, temperature, RAM,
Wi-Fi, volume, battery), event-driven.

**Game tooling** (optional extras):
- `scripts/games`: installer for DRM-free and Steam games (DepotDownloader; Wine or native builds);
- save backups after each game, and sync with a desktop PC's Steam saves (`docs/saves.md`);
- `.claude/commands/setup-game.md`: procedure for getting a Windows game running on the device.

**Maintenance** (optional extras):
- every change is recorded in the repo with a changelog entry;
- `scripts/sync` reports when a live file differs from the repo;
- AUR packages are built only after their diff since the last review is read (`scripts/aur`);
- `/maintain`: an agent procedure for updates, Arch news and checks.

The shared parts are split into **components**, chosen in the host file: `base` (system: network, sudo,
firewall, pacman, fish), `desktop` (sway, foot, the status bar, audio), `inputd` (needs `desktop`), `shelf`
(expects `desktop`). `components=none` applies only the device layer (level 1).

## How to use it

### Fresh install with an agent
1. Boot the Arch install medium on the handheld, connect to Wi-Fi and set a root password.
2. On another computer, open [Claude Code](https://claude.com/claude-code) in a clone of this repo and run
   `/install root@<the handheld's address>`.

The agent identifies the device and asks what you want: components, user name, and which disk to wipe. It
then installs Arch, applies everything and checks the result. The playbook is plain text
([.claude/commands/install.md](.claude/commands/install.md)), so you can also follow it by hand.

### An existing Arch install
As your user:
```sh
git clone <this repo> ~/gpd
mkdir -p ~/.config/gpd
cp -r ~/gpd/users/example ~/gpd/users/$USER        # your own layer; edit it
cat > ~/.config/gpd/host <<HOST
device=gpd-win1
user=$USER
components=base desktop inputd shelf
HOST
sudo pacman -S --needed $(~/gpd/scripts/sync packages)
sudo -v && ~/gpd/scripts/sync install              # links configs, installs system files, builds the programs
sudo systemctl enable $(~/gpd/scripts/sync services | grep -v '^user ')
```
- **Device fixes only**: `user=none` and `components=none` in the host file, and skip the user layer.
  The device layer still replaces some system files (for the GPD: `/etc/mkinitcpio.conf` and the
  systemd-boot entry); `scripts/sync list` shows all of them before you install.
- `scripts/sync install` renames files it would replace to `*.pre-gpd`. Settings files that programs rewrite
  are only replaced with `--force`.
- `scripts/sync check` shows drift; `scripts/sync list` shows which layer each file comes from.

### Just the parts you need
- Read your device's README and docs, and copy the fixes you want.
- Build `src/shelf.c` on its own (`docs/frontend.md`).
- Reuse the Pegasus format notes.

## How the repo is organised
Three layers, applied in this order ([docs/repo-layout.md](docs/repo-layout.md)):

| Layer | Where | What's in it |
|---|---|---|
| Common | the repo root | `src/` programs, `scripts/`, `dotfiles/` and `system/` defaults; `components/<name>/` says which of them each component installs (`manifest.tsv`, `packages.txt`, `services.txt`) |
| Device | `devices/<name>/` | hardware fixes; `device.conf`, the facts the programs read (battery, charger, power limits, which key opens shelf); its docs and patches |
| User | `users/<name>/` | your games, emulator settings and other choices; start from `users/example`, and keep yours in a private repo if you like (`user_dir=` in the host file) |

`~/.config/gpd/host` on each machine names its device, user and components.

## Working with an agent
The repo is written for both people and agents:
- **[CLAUDE.md](CLAUDE.md):** the rules a session follows: every change goes through the repo with a
  changelog entry, and the agent asks before anything risky.
- **`.claude/commands/`:** the playbooks: `/install`, `/port-device`, `/maintain`, `/setup-game`, `/aur-add`.

On the device, shelf's Utilities has **Maintenance** (an agent session running `/maintain`) and **Update
System** (the same without an agent, `scripts/update`).

## Adding a device
Run `/port-device <ssh target>`. The agent surveys the hardware, researches known issues and writes
`devices/<name>/`: README, `device.conf`, fixes, packages and docs. The shared programs either read hardware
facts from `device.conf` or detect them, so a new device normally needs no code changes.

## Status
- GPD Win 1: tested on one unit since October 2026; the docs record what was measured on it.

## Licence
Our own work (programs, scripts, configs, docs, playbooks) is under the **MIT licence** ([LICENSE](LICENSE)).
Parts that modify or come from other projects keep those projects' licences; each folder has a note:

| Part | Licence |
|---|---|
| `devices/gpd-win1/kernel/`: Linux kernel patches | GPL-2.0-only ([note](devices/gpd-win1/kernel/LICENSE.md)) |
| `devices/gpd-win1/mesa/`: Mesa patches; Arch's packaging | MIT; 0BSD ([note](devices/gpd-win1/mesa/LICENSE.md)) |
| `devices/gpd-win1/dxvk/`: DXVK patches | zlib ([note](devices/gpd-win1/dxvk/LICENSE.md)) |
| `aur/pkgbuilds/`: AUR build files | 0BSD ([note](aur/LICENSE.md)) |
| `src/vendor/`: Wayland protocol code | MIT (in the files) |
| `system/etc/systemd/system/systemd-user-sessions.service`: changed copy of systemd's unit | LGPL-2.1-or-later (in the file) |
| `shelf/logos/`: Pegasus grid theme logos | CC BY-NC-SA 4.0, non-commercial ([README](shelf/logos/README.md)) |
| `docs/images/shelf.png`: screenshot | ours, but the game covers and console logos in it belong to their owners |

The licence texts are in `LICENSES/`. Not included: the dxvk-gplasync patches (no licence stated upstream;
`build.sh` downloads them) and the GPD Win's firmware video table.
