# GPD Win 1 handheld — maintenance repo

This repo (`~/gpd`) is the single source of truth for how this device is set up. It is a
dedicated Arch Linux gaming handheld: GPD Win 1 (Atom x7-Z8700 Cherry Trail, 3.7 GB RAM, 58 GB
eMMC, 5.5" 1280x720 panel that is natively portrait), running sway + shelf (our own frontend, `src/shelf.c`) + RetroArch.
The user is Robin (they/them).

## Rules
- The repo is cloned on the GPD (`~/gpd`) and the desktop (`~/Code/gpdwin`); both push to
  GitHub. Start every session with `git pull --ff-only`.
- **Every change to the system goes through this repo.** Edit, then record it in
  `docs/changelog.md` (date, what, why, how to revert) and update the topic doc in `docs/`.
  Commit when a piece of work is done (`git add -A && git commit`), then `git push`
  (remote `origin` = github.com:robinei/gpdwin, deploy key `~/.ssh/id_ed25519`).
- Files listed in `manifest.tsv` are managed:
  - `link` entries: the live path is a symlink into the repo. Edit the file in the repo.
  - `copy` entries (root files, and files programs rewrite): edit the live file and run
    `scripts/sync capture <path>`, or edit the repo copy and run `scripts/sync install <path>`.
  - New config you create: add it to `manifest.tsv` (`scripts/sync adopt` moves+links user files).
  - `scripts/sync check` must report no drift when you finish.
- Never store passwords or keys in the repo.
- Keep docs factual and current; remove things that are no longer true rather than appending
  contradictions. Findings that cost time to discover belong in the docs.
- Don't reboot, suspend, or restart sway/shelf without asking; the user may be playing.
- Prefer official repo packages. AUR packages only through `scripts/aur` (review, then build).
  Keep the AUR set small. No AUR helper (yay was removed): search with `scripts/aur search`.
  New AUR package: follow `/aur-add`.

## sudo
- The Bash tool has no TTY. `sudo -n` works only when the user has run `sudo -v` in a real
  terminal recently (`timestamp_type=global`, ~5 min). `scripts/maintain` does this and keeps
  it alive while Claude runs. Otherwise ask the user to run `sudo -v` (e.g. `! sudo -v`).
- Never pipe a password into `sudo -S` while stdin is also a heredoc script: three bad tries
  trigger pam_faillock (10 minute lockout of sudo and password logins).
- `makepkg -s` fails under these conditions; `scripts/aur build` installs deps with
  `sudo -n pacman --asdeps` and runs makepkg without `-s`.
- Passwordless rules: `/etc/sudoers.d/20-power` (systemctl reboot/poweroff/suspend/hibernate/
  suspend-then-hibernate), `21-firmware-setup`, `23-wifi-reset`. Sudoers files apply in name order, last match
  wins: NOPASSWD files must sort after `10-wheel`.

## Gotchas
- fish is the login shell. Remote/one-off commands with bash syntax: wrap in `bash -c` or a
  heredoc to `bash`. In fish, never `read ... _` (`_` is read-only); use a named variable.
- `pkill -f PATTERN` also matches your own shell if the pattern is in the command line. Use
  `pkill -x NAME`.
- `swaymsg reload` does not run `exec` lines; start daemons with `swaymsg exec ...`.
  Env for sway IPC from a shell: `SWAYSOCK=$(ls /run/user/1000/sway-ipc.*.sock)`.
- Screenshots for checking UI: `grim` (WAYLAND_DISPLAY=wayland-1).
- RetroArch: `config_save_on_exit = false`. For test runs pass
  `--appendconfig` with `config_save_on_exit = "false"`; `--max-frames` disables throttling.
- The eMMC is `mmcblk0` or `mmcblk2` depending on boot; everything uses UUIDs.
- The gamepad only exists as `Microsoft X-Box 360 pad` when the hardware switch is in gamepad
  mode (otherwise "Mouce for Android" mouse/keyboard).
- Restart shelf with `scripts/restart-shelf` (after `scripts/sync install` rebuilt it).
- When a script you run spawns ssh, give it `</dev/null` or it eats the rest of a heredoc.
- The device is `gpdwin.lan`, the desktop `desktop.lan`. Firewall is on (nftables): new
  listening services need a rule in `system/etc/nftables.conf`.
- **"Sound is gone/broken" → first check `modinfo -n snd_soc_rt5645`**: it must be under `.../updates/dkms/`
  (our patched audio modules, DKMS package `gpd-audio`, `kernel/`, docs/hardware.md "Audio after
  hibernation"). The stock modules lose the sound after every hibernate (codec/DSP not restored);
  if they are in use (kernel update whose DKMS build failed: `dkms status`), reboot into patched ones
  after fixing the build. Otherwise run `scripts/audio-speaker` (no sudo; forces the speaker profile
  and default sink, `auto` undoes it) for a falsely reported headphone plug.
- Never unload/unbind the sound codec at runtime (`rmmod snd_soc_rt5645`, unbinding `i2c-10EC5645:00`):
  it hangs the unload for good (`rt5645_i2c_remove` waits for `rt5645_jack_detect_work`, `rmmod` stuck in
  D state, only a reboot clears it; seen twice, the second time after a kernel oops in that work).
  Reloading just `snd_soc_sst_cht_bsw_rt5645` (while PipeWire is stopped) worked once on a fresh boot.
  Test kernel module changes by installing them (DKMS / `updates/`) and rebooting, not by live swapping.
- Never `pkill wineserver` (SIGTERM): it exits without stopping its helpers, leaving `services.exe`,
  `winedevice.exe`, `explorer.exe /desktop` etc. orphaned (dozens after a test session, ~1 GB). Use
  `wineserver -k`. Game launchers (`exec wine`) and force-kill clean up fine by themselves.
- Claude Code uses ~400 MB of RAM; don't run heavy work while a game is running.
- Verify every edit took effect (grep the file afterwards) before documenting it. Some files
  have CRLF line endings (`/opt/zelda3-git/zelda3.ini`), so `sed` patterns ending in `$` miss.

## Map
- `docs/hardware.md` quirks and fixes · `docs/power.md` sleep/idle/battery/turbo ·
  `docs/boot.md` boot chain and timings · `docs/desktop.md` sway/bar/OSD/terminal ·
  `docs/frontend.md` shelf, RetroArch, games · `docs/packages.md` package set and AUR policy ·
  `docs/steam-library.md` owned DRM-free Steam games, tiered for this device ·
  `docs/saves.md` save locations GPD/desktop ·
  `docs/ideas.md` not done yet · `docs/changelog.md` history ·
  `docs/archive/` the original logs this repo was built from.
- `scripts/`: `sync` (manifest), `aur` (review/build), `update` (manual updates), `games`
  (game installer UI, `gamelib/`),
  `maintain` (Claude maintenance launcher), `news`, `fetch-boxart.py`.
- `src/`: single-file C programs (`statusbar` = swaybar status line, `inputd` = Menu key, pad activity, lid, power button),
  built to `~/.local/bin` by `scripts/sync install` (manifest `build`). See docs/desktop.md.
- `aur/`: `reviewed.tsv`, `pkgbuilds/` (vendored build files), `patches/` (our changes).
- `.claude/commands/maintain.md` is the routine maintenance procedure (`/maintain`);
  `.claude/commands/aur-add.md` installs a new AUR package the reviewed, vendored way (`/aur-add`).
- `desktop/`: tools that run on the Linux desktop (clone in `~/Code/gpdwin` there), e.g.
  `steam-cloud-status`. Not used on the GPD.
