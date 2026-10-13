# Handheld Arch

Configuration, fixes and small programs for running Arch Linux on handheld gaming PCs, applied on top of a
standard Arch install. Supported device: **GPD Win 1** (2016, Intel Atom x7-Z8700). Other devices can be added
as a device layer (see "Adding a device").

## Contents

**Device fixes.** Each device folder lists problems on stock Arch, the fix, and measurements. GPD Win 1
([devices/gpd-win1](devices/gpd-win1/README.md)):
- backlight control, panel rotation, suspend and hibernate (sound, charging and brightness restored after resume);
- a patched i915 driver that resyncs the panel when the picture splits;
- a Mesa build with fewer display flashes, Vulkan 1.3 and HiZ on hasvk;
- a speaker compressor, a battery charge limit, boot to the game list in about 22 s.

**shelf** (`src/shelf.c`), a game frontend:
- one C file using SDL3, native Wayland;
- reads gamepads through evdev;
- library in Pegasus Frontend's format (`metadata.pegasus.txt`, `game_dirs.txt`, `media/`, `stats.db`), so
  Pegasus collections, including those EmuDeck generates for Pegasus, can be used unchanged
  ([docs/pegasus-format.md](docs/pegasus-format.md));
- per-game settings for CPU/GPU turbo, a package power limit and a frame-rate cap; play time and favourites.

**inputd** (`src/inputd.c`): idle policy (dim, screen off, suspend), lid, power button, and a key that opens
shelf. Gamepad input counts as activity.

**statusbar** (`src/statusbar.c`): status line for swaybar (CPU/GPU clock, power limit, temperature, RAM,
Wi-Fi, volume, battery), event-driven.

**Game tooling:**
- `scripts/games`: installer for DRM-free and Steam games (DepotDownloader; Wine or native builds);
- save backups after each game, and sync with a desktop PC's Steam saves (`docs/saves.md`);
- `.claude/commands/setup-game.md`: procedure for getting a Windows game running on the device.

**Maintenance:**
- every change is recorded in the repo with a changelog entry;
- `scripts/sync` reports when a live file differs from the repo;
- AUR packages are built only after their diff since the last review is read (`scripts/aur`);
- `/maintain`: an agent procedure for updates, Arch news and checks.

The shared parts are split into **components** (`base`, `desktop`, `inputd`, `shelf`), each optional except
`base`.

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

The licence texts are in `LICENSES/`. Not included: the dxvk-gplasync patches (no licence stated upstream;
`build.sh` downloads them) and the GPD Win's firmware video table.
