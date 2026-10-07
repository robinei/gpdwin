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
- 2026-10-06: Zelda 3 `AudioFreq` 44100 → 48000 (`/opt/zelda3-git/zelda3.ini`, CRLF line endings!) to match PipeWire
  and avoid resampling, as with DevilutionX. Revert: set it back to 44100.
- 2026-10-06: Zelda 3 `OutputMethod` SDL → OpenGL (needed for shaders), `Shader` = RetroArch's
  `shaders_glsl/handheld/lcd3x.glslp` (tested: loads, LCD grid visible). A separate clone of
  snesrev/glsl-shaders was made and deleted again, since RetroArch's set works. Revert:
  `OutputMethod = SDL`, `Shader =` (CRLF file).
- 2026-10-06: `Mod4+F10` runs `screen-reset.sh` (output power off/on) to fix the split picture
  that game fullscreen switches sometimes cause.
- 2026-10-06: `Mod+Shift+Backspace` force-closes the focused window (`force-kill.sh`: SIGTERM to
  its process, SIGKILL after 1 s) for apps that ignore `kill`.

- 2026-10-06: AUR `steamdepotdownloader-bin` updated to 3.4.0-2 (reviewed AUR commit 16ca53438ecd). First install; DepotDownloader for downloading owned Steam games without the client.
- 2026-10-07: installed `wine` 11.19 (+ `ntsync-autoload`), 589 MiB. Shared default prefix
  planned, not created yet; no DXVK. See frontend.md "Windows games".
- 2026-10-07: added `docs/steam-library.md`: owned Steam games that PCGamingWiki lists as DRM-free,
  tiered by expected performance here, with AppIDs for DepotDownloader.
- 2026-10-07: `scripts/games` game installer (curses): browse the Steam library with DRM/tier/
  controller filters, install via DepotDownloader into `~/Games/installed`, auto-add to Pegasus PC
  Games with cover art and a Wine/native launcher. Utilities > Games. GOG source planned.
- 2026-10-07: captured RetroArch settings saved from its menu (see git diff of dotfiles/retroarch/retroarch.cfg).
- 2026-10-07: bar shows volume / mute; the volume keys signal the bar script (SIGUSR1) to redraw
  immediately instead of waiting for the 20 s tick.
- 2026-10-07: audio fixes for Super Meat Boy (native, OpenAL): `~/.config/alsoft.conf` uses the
  pulse backend with 1024-sample periods (PipeWire backend pulsed); PipeWire `min-quantum = 1024`
  (512 underran). Installer: prefers the game's own shebang launch script (no `.sh` needed),
  restores exec bits DepotDownloader drops, and the Pegasus restart escalates to SIGKILL (an
  ignored SIGTERM had left two Pegasus instances). SMB's launcher now runs its own script.
- 2026-10-07: Bastion (native FNA) found no display: its bundled 2015 SDL2 has no Wayland support
  and there is no X server. Moved `Linux/lib64/libSDL2-2.0.so.0` to `.bundled` so the system SDL
  is used (runs). The installer now does this automatically for Linux installs. Bastion's
  launcher now runs its own `Linux/Bastion` script.
- 2026-10-07: `desktop/steam-cloud-status` (runs on the desktop): lists Steam Cloud games whose
  local saves differ from Steam's last sync, i.e. need a Steam launch to upload. Groundwork for
  syncing GPD saves to the desktop (Syncthing) and on to Steam Cloud. Repo cloned on the desktop
  at `~/Code/gpdwin`.
- 2026-10-07: Super Meat Boy and Bastion saves copied from the desktop (Steam Cloud) to the GPD;
  locations and naming differences in `docs/saves.md`. Backups of the GPD's old files in
  `~/save-backups/2026-10-07/`.
- 2026-10-07: Utilities > Save Status (`scripts/save-status` + `desktop/save-status-remote`,
  mapping in `games/saves.json`). The desktop's authorized_keys allows the GPD key to run only the
  read-only status script.
- 2026-10-07: Bastion had no audio: its FMOD Ex uses ALSA, and without `pipewire-alsa` ALSA's
  default was the busy hardware device. Installed `pipewire-alsa` (explicit); `/maintain` now
  checks that both the PulseAudio and ALSA bridges to PipeWire are present.
- 2026-10-07: audio latency: capped the speaker's hardware buffer from ~714 ms (34 periods) to
  ~84 ms (4 x 1008 frames) with a WirePlumber rule; Bastion's lag is gone. Revert: delete
  `~/.config/wireplumber/wireplumber.conf.d/51-speaker-latency.conf`, restart wireplumber.
- 2026-10-07: Hyper Light Drifter's Linux build is 32-bit (no 32-bit loader without multilib), so
  it can't start. Installer now warns about 32-bit Linux builds and suggests the Windows build
  (Wine WoW64 runs 32-bit Windows games without multilib).
- 2026-10-07: Hyper Light Drifter (Windows build via Wine) rendered in a corner: Wine's Wayland
  driver sees the unrotated 720x1280 panel. Installed `xorg-xwayland` so Wine uses X11 (needs a
  sway restart to take effect).
- 2026-10-07: verified after a sway restart: Hyper Light Drifter (Wine, X11 via Xwayland) runs fullscreen correctly.
- 2026-10-07: reviewed ViccRondo/gpd-win1-atomic-gaming; findings in docs/hardware.md "Prior art"
  (confirms xHCI wakeup fix and Vulkan 1.2; DSI resume kernel patch and kernel args noted as
  fallbacks; no DSI pipeline failures in our logs).
- 2026-10-07: core dumps off (`/etc/systemd/coredump.conf.d/90-gpd.conf`: Storage=none,
  ProcessSizeMax=0); crashes are still logged in the journal. Revert: delete the file.

- 2026-10-07: AUR `pegasus-frontend-stable-git` updated to alpha16.r82.gc3462e68-1 (reviewed AUR commit caec261bd57d) with local patches 0001-splash-stop-progress-animation-when-hidden.patch, 0002-grid-theme-stop-hidden-spinner.patch.
- 2026-10-07: improvement pass. Pegasus idle redraw fixed with source patches (60 fps / 33% CPU ->
  0 fps / 2% CPU on the menu); `scripts/aur` gained vendoring (`aur/pkgbuilds/`), build-file and
  source patches (`aur/patches/`), local files outside the repo, sudo keepalive, pristine-source
  builds and `-ffdx` cleanup; Pegasus package now carries its deps and a tolerant .install;
  zelda3-git pinned to 45a149d with `backup=` for zelda3.ini. NMI watchdog off. Freed ~1.4 GB:
  stale yay/aur build dirs, package cache -k1, orphans (qt5-tools, unzip), yay-bin-debug.
- 2026-10-07: `/maintain` AUR step updated for vendoring, patches (what to do when one no longer
  applies), pinned `-git` packages and detached long builds; new `/aur-add` command for
  installing new AUR packages the reviewed, vendored way. yay stays (search only).
- 2026-10-07: removed yay (`yay-bin`, its cache/config and vendored copy); `scripts/aur search`
  replaces `yay -Ss`. git/base-devel stay (needed for `scripts/aur` builds).
- 2026-10-07: `scripts/aur check` reports pinned git sources and how many upstream commits are
  newer (first run: zelda3-git up to date; pegasus-frontend-stable-git's AUR pin c3462e68 is 25
  commits behind upstream).
- 2026-10-07: idle wakeup and power pass. Pegasus patch 0003 (adaptive gamepad polling): idle
  wakeups 72/s -> 20/s, CPU 2.4% -> 0.8%. USB irqs while the screen is on traced to the gamepad's
  4 ms idle reports (inherent). Measured power on battery: screen on 2.56 W, backlight-0 1.92 W,
  display off 1.45 W, so swayidle's DPMS off stays. Details in docs/power.md.

- 2026-10-07: AUR `pegasus-frontend-stable-git` updated to alpha16.r82.gc3462e68-1 (reviewed AUR commit caec261bd57d) with local patches 0001-splash-stop-progress-animation-when-hidden.patch, 0002-grid-theme-stop-hidden-spinner.patch, 0003-gamepad-adaptive-polling.patch.
- 2026-10-07: hardening/cleanup pass: LLMNR+mDNS off (router DNS gives gpdwin.lan; iwd now sends
  the hostname), nftables firewall (ssh from LAN only), journald capped at 200 MB,
  realtime-privileges for PipeWire RT (active after next login), Pegasus without Qt bearer
  polling, `scripts/restart-pegasus`, Save Status uses desktop.lan over IPv4. Corrected: Zelda 3
  saves survive uninstall.
- 2026-10-07: verified after reboot: PipeWire threads SCHED_FIFO via realtime-privileges, no RTKit
  warnings; firewall loaded at boot (nftables.service shows inactive by design, oneshot).
- 2026-10-07: verified CPU boost (2.4 GHz all cores), idle floor (480 MHz, C7), and the Mod4+F11
  CPU turbo toggle (1.6 GHz cap and back); GPU idles in RC6 (0 MHz), boosts above 400 MHz with
  turbo on and respects the 400 MHz cap from Mod4+F12. Details in docs/power.md.
- 2026-10-07: idle screen-off only on battery (`idle-screen-off.sh`); on AC the screen just dims.
- 2026-10-07: `mitigations=off` (syscalls 10x cheaper; HLD uses ~12% less CPU, emulators ~1%). Benchmarks in docs/power.md.
- 2026-10-07: vm.swappiness 10; THP deliberately left at always (reasoning in docs/power.md).
- 2026-10-07: status bar shows CPU/GPU boost (cpu+/gpu+, dimmed cpu-/gpu- when capped); turbo.sh redraws it.
- 2026-10-07: RetroArch refresh rate 60.253 (real panel rate); sway subpixel vbgr (rotated panel).
- 2026-10-07: sway max_render_time 12: dropped refreshes in games from ~13% to 0-0.3% (measured).
- 2026-10-07: sway subpixel none instead of vbgr (grey antialiasing, smaller glyph caches).
- 2026-10-07: status bar shows RAM used/total.
- 2026-10-07: status bar and Guide button rewritten in C (`src/statusbar.c`, `src/inputd.c`, manifest `build`
  entries): bar shows CPU/GPU clocks, updates every second and instantly on events; inputd only wakes
  for the Guide button.
- 2026-10-07: TXE runtime PM (udev rule, ~80 mW); idle suspend after 20 min instead of 10.
- 2026-10-07: game installer: platforms from the Steam store (batched, no key; filled for the whole
  library), only builds that exist are offered; Linux downloads use `-osarch 64`; a 32-bit-only
  Linux build is removed, marked `linux32` and the Windows build offered instead; store size
  estimate shown in the list and the game menu.
- 2026-10-07: installer shows the exact download/disk size (DepotDownloader -manifest-only) and free
  space before installing, then asks Install? [Y/n].
- 2026-10-07: every tiled window on workspace 1 is fullscreen; Pegasus gets fullscreen back after games.
