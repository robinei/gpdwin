# GPD Win 1 handheld — maintenance repo

This repo (`~/gpd`) is the single source of truth for how this device is set up. It is a
dedicated Arch Linux gaming handheld: GPD Win 1 (Atom x7-Z8700 Cherry Trail, 3.7 GB RAM, 58 GB
eMMC, 5.5" 1280x720 panel that is natively portrait), running sway + Pegasus + RetroArch.
The user is Robin (they/them).

## Rules
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
- Don't reboot, suspend, or restart sway/Pegasus without asking; the user may be playing.
- Prefer official repo packages. AUR packages only through `scripts/aur` (review, then build).
  Keep the AUR set small. Never `yay -S` / `yay -Syu` for AUR builds.

## sudo
- The Bash tool has no TTY. `sudo -n` works only when the user has run `sudo -v` in a real
  terminal recently (`timestamp_type=global`, ~5 min). `scripts/maintain` does this and keeps
  it alive while Claude runs. Otherwise ask the user to run `sudo -v` (e.g. `! sudo -v`).
- Never pipe a password into `sudo -S` while stdin is also a heredoc script: three bad tries
  trigger pam_faillock (10 minute lockout of sudo and password logins).
- `makepkg -s` fails under these conditions; `scripts/aur build` installs deps with
  `sudo -n pacman --asdeps` and runs makepkg without `-s`.
- Passwordless rules: `/etc/sudoers.d/20-power` (systemctl reboot/poweroff/suspend/hibernate/
  suspend-then-hibernate), `21-firmware-setup`. Sudoers files apply in name order, last match
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
- Claude Code uses ~400 MB of RAM; don't run heavy work while a game is running.

## Map
- `docs/hardware.md` quirks and fixes · `docs/power.md` sleep/idle/battery/turbo ·
  `docs/boot.md` boot chain and timings · `docs/desktop.md` sway/bar/OSD/terminal ·
  `docs/frontend.md` Pegasus, RetroArch, games · `docs/packages.md` package set and AUR policy ·
  `docs/ideas.md` not done yet · `docs/changelog.md` history ·
  `docs/archive/` the original logs this repo was built from.
- `scripts/`: `sync` (manifest), `aur` (review/build), `update` (manual updates),
  `maintain` (Claude maintenance launcher), `news`, `fetch-boxart.py`.
- `.claude/commands/maintain.md` is the routine maintenance procedure (`/maintain`).
