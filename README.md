# Handheld Arch

Plain Arch Linux on gaming handhelds, made to feel like a console: power on, pick a game, play. There's no
image to flash and no distribution to trust. Instead, this repo is a documented set of fixes, small programs
and configs on top of stock Arch. One script applies them, and an AI agent maintains them over ssh.

It started on a **GPD Win 1** (2016, Intel Atom, 4 GB). Stock Linux handles that device badly; with this
setup it boots quickly into a quiet game console. An **ASUS ROG Ally** is next. Other devices can be added
as a new device layer (see "Adding a device").

## What you get

**Fixes for your hardware.** Each device has its own folder listing what's wrong on stock Arch and how it's
fixed, with measurements. For the GPD Win 1 ([devices/gpd-win1](devices/gpd-win1/README.md)):
- backlight, rotation, and suspend/hibernate (sound, charging and brightness survive it);
- a split-screen fix in the display driver;
- an own Mesa build with fewer display flashes and real Vulkan 1.3;
- a speaker compressor and a battery charge limit;
- about 22 s from power button to the game list.

Even if you use nothing else here, those notes can save you days.

**shelf**, a game frontend (`src/shelf.c`):
- one C file, native Wayland, starts in a fraction of a second;
- reads gamepads directly and stays out of the way while a game runs;
- keeps its library in **Pegasus Frontend's format** (`metadata.pegasus.txt`, `game_dirs.txt`, `media/`), so
  collections from a Pegasus or EmuDeck-with-Pegasus setup work as they are, and you can switch back any
  time ([docs/pegasus-format.md](docs/pegasus-format.md));
- per game: CPU/GPU turbo, a power limit and a frame-rate cap; it also keeps play time and favourites.

**inputd**, the handheld's power policy (`src/inputd.c`). It dims the screen, turns it off and sleeps when
idle. The gamepad counts as activity, and games can keep it awake. It also handles the lid and the power
button, and opens shelf from a key.

**A status bar** that costs almost nothing (`src/statusbar.c`).

**Games beyond emulators:**
- an installer for your DRM-free and Steam games: DepotDownloader, Wine or native builds, cover art
  (`scripts/games`);
- save backups after every game, and a sync with a desktop PC's Steam saves (`docs/saves.md`);
- a playbook for getting each Windows game to run well on weak hardware (`.claude/commands/setup-game.md`).

**A way to maintain it:**
- every change goes through this repo with a changelog entry;
- `scripts/sync` shows when a live file has drifted from the repo;
- AUR packages are built only after their diff is reviewed;
- `/maintain` has an agent run updates, check Arch news and do the checks.

All of it is optional and separable. The shared parts are **components** you pick (`base`, `desktop`,
`inputd`, `shelf`), so you can take the hardware fixes and keep your own desktop, or use shelf alone.

## How to use it

### Fresh install with an agent (the intended way)
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
- **Nothing is lost:** `scripts/sync install` moves anything it would replace aside (`*.pre-gpd`), and
  never overwrites settings a program saved without asking you first.
- **Checking:** `scripts/sync check` shows drift; `scripts/sync list` shows which layer each file comes from.

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

With ssh access and these, a request like "make Dolphin use Vulkan" or "why does the battery drain" becomes
a change you can read, keep or revert.

On the device, shelf's Utilities has **Maintenance** (an agent session running `/maintain`) and **Update
System** (the same without an agent, `scripts/update`).

## Adding a device
Run `/port-device <ssh target>`. The agent surveys the hardware, researches known issues and writes
`devices/<name>/`: README, `device.conf`, fixes, packages and docs. The shared programs either read hardware
facts from `device.conf` or detect them, so a new device normally needs no code changes.

## Status
- **GPD Win 1:** in daily use since October 2026 on one unit; the docs record what was measured on it. That
  unit currently has a hardware fault ([devices/gpd-win1](devices/gpd-win1/README.md), "Known problems").
- **ROG Ally (Z1 Extreme):** planned (`docs/ideas.md` item 15).
- **Licence:** not chosen yet. The collection logos in `shelf/logos` come from Pegasus' grid theme
  (CC BY-NC-SA), and vendored code keeps its own licences.
