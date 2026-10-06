# Changelog

Newest last. Each entry: what changed, why, and how to revert if it isn't obvious.
Details before 2026-10-06 are in `docs/archive/`.

## 2026-10-06 — install and setup (installer session, remote over ssh)
- Wiped the eMMC (old Arch install), new GPT: 512 MiB ESP / 4 GiB swap / ext4. Installed a
  minimal Arch with systemd-boot, iwd + resolved, fish, sway, foot, fuzzel, PipeWire.
- Hardware fixes: backlight (pwm-lpss in initramfs), rotation, xHCI wakeup (suspend), backlight
  restore after hibernate, wireless-regdb + NO regdomain.
- Sleep: s2idle, suspend-then-hibernate after 30 min on lid/power key; low-battery hibernate
  timer; swayidle dim/off/suspend; Bluetooth blocked; fstrim and paccache timers.
- sudo: `10-wheel`, `20-power` (NOPASSWD power commands, must sort after 10-wheel). sshd key-only.
- Desktop: Catppuccin theme, 1 px borders, wallpaper, JSON bar with Wi-Fi/battery/clock, wob OSD,
  hidden cursor, foot translucency, Guide-button helper.
- Frontend: Pegasus (AUR, built with makepkg), dbus-send shim for its power menu, collections
  pc/snes/utils, box art script. RetroArch + 8 cores, pad hotkeys, frame pacing tuning.
  DevilutionX (48 kHz fix).

## 2026-10-06 — tweaks (on-device session)
- Logins no longer wait for `network.target` (full override of systemd-user-sessions.service):
  boot 24.4s → 21.9s, getty 2.7s earlier.
- CPU/GPU turbo toggles on Mod4+F11/F12 with wob feedback; tmpfiles rule for write access.
- `helix` as EDITOR/VISUAL. zram tried and removed (zswap already on).
- `21-firmware-setup` sudo rule + Pegasus "BIOS Setup" tool.
- Zelda 3 (zelda3-git) with 16:9 / quality-of-life settings, added to the PC collection.
- Fixed `read -P ... _` in the Utilities scripts (fish rejects `_`).

## 2026-10-06 — maintenance repo
- Created `~/gpd`: docs split from `~/tweaks.md` and the installer log, `manifest.tsv` + `scripts/sync`
  (user configs symlinked into the repo, system files tracked as copies), `scripts/aur`
  (review-before-build, `aur/reviewed.tsv`), `scripts/update` (manual), `scripts/maintain` +
  `/maintain` (Claude), `scripts/news`.
- Pegasus Utilities: "Update System" now runs `scripts/update`; "Update Pegasus" replaced by
  "Maintenance" (`scripts/maintain`). fish `pegasus-update` function removed (use `scripts/aur`).
- `~/tweaks.md` moved to `docs/archive/tweaks.md` (a symlink remains at `~/tweaks.md`).
- 2026-10-06: repo pushed to private GitHub `robinei/gpdwin` (deploy key `~/.ssh/id_ed25519`,
  GitHub ed25519 host key verified against the published fingerprint and added to known_hosts).
- 2026-10-06: fixed "claude: command not found" from Pegasus' Maintenance: `~/.local/bin` was
  added to PATH in fish's `config.fish`, which runs after `conf.d/sway.fish` has already
  exec'd sway. Moved to `conf.d/00-path.fish`; `scripts/maintain` also adds it. Takes effect for
  the whole session at the next login (the maintain script works now). `sync check` now skips
  root-only files without sudo instead of failing.

## 2026-10-06 — maintenance pass
- Arch news: nothing new needing action. AUR packages all up to date.
- `pacman -Syu`: firefox 157.0.1, linux 7.2.9 (reboot needed), openssh 10.6p1 (sshd restarted).
- No pacnew files, no failed units. Journal errors were the known brcmfmac/ACPI noise and
  sudo password-required entries from read-only checks without a sudo timestamp.
- Orphans left in place (asked): qt5-tools, unzip. `~/.cache/yay` is ~1 GB (revert: n/a, cache only).

## 2026-10-06 — maintenance pass (second)
- Arch news: nothing new. No repo or AUR updates pending, so no upgrade was run.
- No pacnew, no failed units; journal errors only the known brcmfmac noise. Disk 25% used.
- Orphans still left in place: qt5-tools, unzip. Running kernel 7.2.8, installed 7.2.9: reboot still pending.
