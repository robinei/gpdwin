# Repo layout: common, device and user layers (built 2026-10-13)

The repo grew around one device (GPD Win 1) and one person. A second device (ASUS ROG Ally, Z1 Extreme, no
keyboard; `docs/ideas.md` item 15) and the wish to share the work with others pull in different directions:

- the GPD's needs (Intel Atom, patched Mesa/DXVK, charger by voltage, keyboard);
- the Ally's needs (Ryzen, stock Mesa and Proton, pad and extra buttons only);
- no duplication between them;
- Robin's own choices (games, emulator settings, look);
- an entry point for other people who want Arch to work well on their GPD or Ally: boot the install
  medium, start ssh, point an agent at it (`/install`), and get a fully fixed system.

Answer: three layers, applied in order, with the shared part split into optional components.

## Layers
```
(repo root)          common: everything device- and person-neutral
  src/               shelf, inputd, statusbar (+ devconf.h: reads device.conf)
  scripts/           sync, layers.py, game-wrap, games + gamelib, saves-*, steam-cloud, update, maintain, ...
  dotfiles/ system/  configs and /etc files the components install
  shelf/ pegasus/utils/ games/{patches,shims}/ aur/ desktop/
  components/<name>/ base, desktop, inputd, shelf: manifest.tsv, packages.txt, services.txt, sway.conf
  .claude/commands/  install, port-device, maintain, setup-game, aur-add
  docs/              frontend, desktop, saves, packages, pegasus-format, ideas, changelog, this file
devices/<name>/      one per device (gpd-win1 so far)
  README.md          how to recognise it, what is fixed, extras not from pacman, known problems
  device.conf        hardware facts the programs read (below)
  manifest.tsv packages.txt services.txt
  system/ dotfiles/  boot entry, mkinitcpio, udev rules, sleep hooks, sway.conf (outputs), audio tuning
  kernel/ mesa/ dxvk/   gpd-win1 only
  docs/              hardware, power, boot, archive
users/<name>/        one per person: robin, example (the template)
  manifest.tsv packages.txt
  dotfiles/ pegasus/ games/ system/ docs/   emulator settings, collections, saves.json, Wi-Fi country, notes
```

The common layer stayed at the repo root rather than moving into `common/`: generated game launchers on the
device call `~/gpd/scripts/game-wrap`, and every doc and script refers to `scripts/`, `src/`, `docs/`.

A file belongs to the lowest layer that is true for it: if every device and person would want it, common
(in the component it belongs to); if it depends on the hardware, the device; if it's a choice, the user.

## How the layers combine
- **Host file** `~/.config/gpd/host` (not in the repo; `scripts/layers.py`):
  `device=gpd-win1`, `user=robin` (or `user_dir=<path>` for a user layer kept elsewhere, e.g. a private
  repo), `components=base desktop inputd shelf` (default: all four; `none`: no component, only the device fixes).
  `user=none`: no user layer. Without `user=` the only users/* other than example is used; without `device=` `sync install` refuses (it would leave out the hardware fixes).
- **Manifests:** `scripts/sync` reads each component's `manifest.tsv` in the listed order, then the device's,
  then the user's. Repo paths are relative to the layer's root (the repo root for components). A later layer
  may name the same live path and wins. `sync list` shows each live file's layer. Owner `user` = whoever runs
  sync. Copies containing `@USER@` or `@ROOT_UUID@` are templates filled in on install (capture skips them).
  A link that still points into the repo at an old path is simply re-pointed by `install`.
- **Packages and services:** `packages.txt` and `services.txt` next to each manifest; `sync packages` and
  `sync services` print the union for this host (the install playbook uses them).
- **sway:** the common config ends with `include ~/.config/gpd/sway.d/*`; layers link their snippets there:
  `10-device.conf` (outputs, rotation, device keys), `50-shelf.conf`, `50-inputd.conf` (their autostart),
  `90-user.conf` (optional).
- **Python scripts** find user data with `layers.user_file("games/saves.json")`.

## Components
| Component | What | Needs |
|---|---|---|
| base | shell, sudo rules (group wheel), network (iwd, resolved), firewall, sshd, pacman, logging, low-battery hibernate | - |
| desktop | autologin into sway, foot, fuzzel, wob, status bar, PipeWire defaults, GPU driver prefetch | base |
| inputd | idle dim/off/sleep, lid, power button, menu key; logind leaves those to it | desktop (sway IPC) |
| shelf | the frontend, Utilities collection, per-game performance (turbo/power/fps), MangoHud, save backup timer | desktop (plain Wayland; the fullscreen rules are sway's) |

Without inputd, logind's defaults handle the lid and power button and nothing dims the screen (add swayidle in
a user sway snippet). Without shelf, nothing starts on workspace 1.

## device.conf
`key=value`, `#` comments, values with spaces in quotes (shell scripts source it). Read by statusbar, inputd
and shelf (`src/devconf.h`) and game-wrap. Every key is optional; the code never asks which device it runs
on. Implemented keys (the GPD Win's values):

| Key | gpd-win1 | Default without it | Used by |
|---|---|---|---|
| `battery` | `max170xx_battery` | first power supply of type Battery | statusbar |
| `charger` | `bq24190-charger` | first Mains/USB supply with `online` | statusbar, inputd |
| `wifi` | `wlan0` | first interface with `wireless/` | statusbar |
| `temp_hwmon` | `coretemp` | coretemp, else k10temp | statusbar |
| `power_limit` | (default) | Intel RAPL PL1 `constraint_0_power_limit_uw` | statusbar, game-wrap |
| `power_default_w` | `6.016` | statusbar: the value at its start; game-wrap: 6.016 | statusbar, game-wrap |
| `power_steps_w` | `"8 1 2 4"` | `8 1 2 4` | shelf (W key) |
| `menu_key` | `127` (KEY_COMPOSE) | 127 | inputd |
| `pad_name` | `"Microsoft X-Box 360 pad"` | the same | inputd |

Hardware without a counterpart degrades instead of failing: no i915 GPU clock files → no GPU block in the
bar and no lid throttling; `cpufreq/boost` is used where intel_pstate's `no_turbo` is missing (turbo.sh,
game-wrap, statusbar). Still Intel-only: GPU turbo/pin (i915 `rps_*`), the power limit (RAPL). For the
Ally these need an AMD path (amdgpu `power_dpm_force_performance_level`, asus-wmi `ppt_*`), added when it
is ported, with its keys documented here.

## Docs
- Device findings live with the device (`devices/*/docs`). They are the most reusable work: hasvk HiZ, the DXVK
  readback stall, charge limit by voltage, the 2026-10-12 4 A drain if its cause is found.
- Play history and per-game notes are personal (`users/robin/docs`).
- One changelog for now (`docs/changelog.md`, all layers; entries say which device a change was tested on).
  Split per layer when users/robin moves to its own repo.
- `README.md` at the top is for outsiders: what this is, "pick your device", the install path, and how to work with
  an agent. `CLAUDE.md` says which layers a session may change and where things go.

## Formats
- **Game library: Pegasus's format**, not our own: `metadata.pegasus.txt` per collection, `game_dirs.txt`,
  `media/<file>/boxFront.png`, `stats.db` (play history). shelf already implements it (`docs/pegasus-format.md`),
  so an existing Pegasus setup (EmuDeck offers Pegasus as a frontend) can try shelf on the same files and go back.
  For adoption, shelf should also cover the parts marked "unused here" in that spec (`metafiles/`, `regex`,
  multi-file games, ...), and check that EmuDeck's generated files load unchanged (not yet looked at).
  Possibly later: ES-DE's `gamelist.xml`, EmuDeck's default frontend, read the way Pegasus reads it.
- **Our own data**, where no standard exists: keep the simplest form that fits. Line-based `key=value` for
  `device.conf` and the per-game settings (also readable from shell); JSON where there is nesting (`saves.json`,
  game records). Per-game settings stay in their own file (only values that differ from the defaults), not as
  `x-` keys in metadata, so the library files stay shareable and untouched.

## Agent playbooks
The Claude playbooks are part of what others get: `/install` (from the Arch install medium over ssh: identify
the device, ask for components and the disk, install, apply the layers, check), `/port-device` (survey new
hardware, write `devices/<new>/`), `/maintain`, `/setup-game`, `/aur-add`. "Install Arch, then let an agent over
ssh make it yours" is the intended entry point, rather than an image to flash.

## Public vs private
- Public repo: the common layer, `devices/`, `users/example/`. Private: `users/robin/` as its own repo, cloned next to it
  (the host file points at it), so personal files never enter public history.
- Licence (2026-10-13): MIT for our work (`LICENSE`); third-party parts keep theirs, noted per folder and in
  README "Licence" (kernel patches GPL-2.0-only, Mesa MIT, DXVK zlib, AUR/Arch packaging 0BSD, logos CC BY-NC-SA).
  Material without a licence or from firmware is kept out (dxvk-gplasync patches are downloaded by `build.sh`;
  the GPD's `vbt.bin` stays local). New third-party files need the same check before they are committed.
- Start the public repo from the layered tree (fresh history) or audit the history first: it has paths, game
  lists and save details, never secrets (the sudo password and Steam key were never committed; check anyway).
- Rename from `gpdwin` to something device-neutral.

## Steps
1. Done 2026-10-13: `device.conf` + host file; statusbar, inputd, shelf, game-wrap, lid.sh, turbo.sh read it or
   detect the hardware.
2. Done: no hard-coded user in manifests (`user` owner, `@USER@`), sudoers by group wheel, user data found
   through the host file.
3. Done: files moved into `components/`, `devices/gpd-win1/`, `users/robin/`; layered `scripts/sync`;
   packages and services per layer; `/install` and `/port-device` playbooks; README for outsiders.
4. Open: `users/robin/` into its own private repo (`user_dir=`), once the public repo exists.
5. Open: the Ally as the second device (`devices/rog-ally/`, via `/port-device`): the real test of the split.
6. Licence done (2026-10-13). Open: rename from `gpdwin`, history audit (the removed files are still in the
   history: start the public repo fresh or rewrite it), go public.
