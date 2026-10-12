# Repo layout: shared, device and user layers (plan, 2026-10-12)

The repo grew around one device (GPD Win 1) and one person. A second device (ASUS ROG Ally, Z1 Extreme, no
keyboard; `docs/ideas.md` item 15) and the wish to share the work with others pull in different directions:

- the GPD's needs (Intel Atom, patched Mesa/DXVK, charger by voltage, keyboard);
- the Ally's needs (Ryzen, stock Mesa and Proton, pad and extra buttons only);
- no duplication between them;
- Robin's own choices (games, emulator settings, look);
- an entry point for other people who want Arch to work well on their GPD or Ally.

Answer: three layers, each a directory, applied in order. Not started; steps at the end.

## Layers
```
common/              everything device- and person-neutral
  src/               shelf, inputd, statusbar
  scripts/           sync, game-wrap, games + gamelib, saves-*, steam-cloud, update, maintain, ...
  dotfiles/          sway base config, foot, fish, wob, MangoHud, emulator defaults
  system/            /etc files every device wants (pacman, iwd, tmpfiles for wheel access, ...)
  shelf/             logos, shelf assets
  .claude/commands/  maintain, setup-game, port-device
  docs/              frontend, saves, packages (common part), changelog (common)
  manifest.tsv
devices/<name>/      one per device: gpd-win1, rog-ally
  device.conf        hardware facts the common code reads (below)
  system/, dotfiles/ kernel modules/params, udev rules, services, sway device.d/ snippets
  kernel/, mesa/, dxvk/   gpd-win1 only
  docs/              hardware, power, boot, findings; its own changelog
  manifest.tsv
users/<name>/        one per person: robin (private), example (public template)
  dotfiles/          sway personal.d/, emulator settings kept by choice, theme
  games/             collections metadata, saves manifest, steam library
  docs/              play notes, game setup notes (steam-library.md), own changelog
  manifest.tsv
```

A file belongs to the lowest layer that is true for it: if every device and person would want it, `common/`; if it
depends on the hardware, `devices/`; if it's a choice, `users/`.

## How the layers combine
- **Host file** `~/.config/gpd/host` (not in the repo): `device=gpd-win1` and `user=robin`.
- **Manifest:** `scripts/sync` reads common, then the device's, then the user's `manifest.tsv`; a later layer may
  name the same live path and wins (a user's `retroarch.cfg` over the common default). `sync check` says which layer
  each live file comes from.
- **sway:** the common config ends with `include device.d/*` and `include personal.d/*` (linked from the device and
  user layers).
- **No hard-coded person:** `$HOME` and `$USER` in manifests, scripts and configs instead of `/home/robin` and
  `robin:robin`; the program looks paths up (e.g. game dirs relative to `$HOME`).

## device.conf
Plain `key=value`, read at start by shelf, inputd, statusbar and game-wrap (and sourced by shell scripts). The code
never asks which device it runs on, only for these facts; a new device is a new file, not new code. Draft keys:

| Key | gpd-win1 | rog-ally |
|---|---|---|
| `power_limit` (sustained package limit file) | `/sys/class/powercap/intel-rapl:0/constraint_0_power_limit_uw` | asus-wmi `ppt_pl1_spl` (or `asus-armoury`) |
| `power_default_w` / `power_steps_w` (shelf W) | 6 / 1 2 4 6 8 | ~15 / 9 15 20 25 30 |
| `power_draw` (energy counter) | `intel-rapl:0/energy_uj` | `amdgpu` hwmon power |
| `cpu_turbo` | `intel_pstate/no_turbo` | `cpufreq/boost` |
| `gpu_freq_min/max` (shelf G/P) | i915 `rps_*_freq_mhz` | amdgpu `power_dpm_force_performance_level` / `pp_od_clk_voltage` |
| `temp_sensor` | coretemp | k10temp |
| `charge_limit` | `voltage:4144000` (bq24190) | `threshold:80` (`charge_control_end_threshold`) |
| `menu_key` (opens shelf) | `KEY_COMPOSE` | the Ally's left extra button |
| `quick_key` | (none) | the right extra button |
| `rotation`, `idle_*` | 270, power.md times | 0, to measure |

Exact names get settled in step 1, when the code starts reading the file.

## Docs
- Device findings live with the device (`devices/*/docs`). They are the most reusable work: hasvk HiZ, the DXVK
  readback stall, charge limit by voltage, the 2026-10-12 4 A drain if its cause is found.
- Play history and per-game notes are personal (`users/robin/docs`).
- One changelog per layer; the common one notes which devices a change was tested on.
- `README.md` at the top is for outsiders: what this is, "pick your device", the install path, and how to work with
  an agent. `CLAUDE.md` says which layers a session may change and where things go.

## Agent playbooks
The Claude playbooks are part of what others get: `/maintain`, `/setup-game`, and a new `/port-device`: boot Arch
with ssh, the agent detects the hardware, writes `devices/<new>/` (device.conf, fixes, docs) and a `users/<you>/`
from the example. "Install Arch, then let an agent over ssh make it yours" is the intended entry point, rather than
an image to flash.

## Public vs private
- Public repo: `common/`, `devices/`, `users/example/`. Private: `users/robin/` as its own repo, cloned next to it
  (the host file points at it), so personal files never enter public history.
- Licence for our own code to be chosen. The grid-theme logos are CC BY-NC-SA (non-commercial): ship them with
  that note or fetch them at install. Vendored code keeps its licences.
- Start the public repo from the layered tree (fresh history) or audit the history first: it has paths, game
  lists and save details, never secrets (the sudo password and Steam key were never committed; check anyway).
- Rename from `gpdwin` to something device-neutral.

## Steps (each leaves the GPD working)
1. `device.conf` for gpd-win1 + the host file; shelf, inputd, statusbar and game-wrap read their hardware facts from
   it. Nothing moves. Can be built and tested on the desktop.
2. `$HOME`/`$USER` instead of the hard-coded user in manifests, scripts and configs.
3. `git mv` into `common/`, `devices/gpd-win1/`, `users/robin/`; split the manifest; layered `scripts/sync`; fix doc
   and script paths. Moves nearly every file: do it when no other session is working on the repo.
4. `users/robin/` to its own private repo; `users/example/`.
5. The Ally as the second device (`devices/rog-ally/`, via `/port-device`): the real test of the split.
6. README for outsiders, licence, rename, go public.
