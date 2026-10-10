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
- 2026-10-07: every tiled window on workspace 1 is fullscreen; the window below gets fullscreen back when the top one closes (`fullscreen-stack.sh`).

- 2026-10-07: AUR `pegasus-frontend-stable-git` updated to alpha16.r82.gc3462e68-1 (reviewed AUR commit caec261bd57d) with local patches 0001-splash-stop-progress-animation-when-hidden.patch, 0002-grid-theme-stop-hidden-spinner.patch, 0003-gamepad-adaptive-polling.patch, 0004-grid-theme-starting-overlay.patch.
- 2026-10-07: installer covers from the store asset list (newer games keep art under hashed paths; Heretic + Hexen had none).
- 2026-10-07: Heretic + Hexen set to fullscreen in its own config (v_windowmode 2); windowed-mode games undo sway fullscreen.
- 2026-10-07: brightness floor 1 instead of 2 (key and idle dim; checked visible).
- 2026-10-07: second idle dim level: 1% after 10 min.
- 2026-10-07: battery idle: 1% at 3 min, screen off at 5 min (was off at 3).
- 2026-10-07: Psychonauts (Windows) runs: DisplaySettings.ini at 1280x720; Xwayland's emulated modes are rotated (no 640x480).
- 2026-10-07: MangoHud (FPS, CPU/GPU clock), opt-in per game launcher; Psychonauts uses it.
- 2026-10-07: MangoHud replaced by `mangohud-light` (vendored Arch PKGBUILD, light patch); loaded into all games via Pegasus, hidden until Right Shift+F12.

- 2026-10-07: AUR `mangohud-light` updated to 0.8.4-1 (reviewed AUR commit aec701d04109).
- 2026-10-07: multilib enabled with the usual 32-bit graphics/audio/X11 set (system upgraded too); installer offers 32-bit Linux builds again.

- 2026-10-07: AUR `lib32-openal` updated to 1.25.2-2 (reviewed AUR commit c1f1bf20b2e7).
- 2026-10-07: lib32-openal (SteamWorld Heist); installer writes steam_appid.txt for Steam API games so they run without the Steam client.
- 2026-10-07: gamepad use keeps the screen awake: inputd pokes sway (zero cursor move) on pad input, at most every 30 s.
- 2026-10-07: native Psychonauts windowed 1280x720 + floating X11 game windows on ws1 made fullscreen; lib32-mangohud.

- 2026-10-07: AUR `lib32-sdl2-compat` updated to 2.32.74-1 (reviewed AUR commit 9b3db2d26721).

- 2026-10-07: AUR `lib32-sdl12-compat` updated to 1.2.68-2 (reviewed AUR commit 90b185b9f12e).
- 2026-10-07: lib32-sdl2-compat + lib32-sdl12-compat; installer swaps bundled SDL 1.2/2 for the system compat libs (per architecture); Psychonauts camera axes fixed.
- 2026-10-07: HLD switched to the native build (run.sh, saves moved from the Wine prefix); installer prefers games' own launch scripts.

## 2026-10-08 — maintenance pass
- No repo updates pending (kernel 7.2.9 running). No AUR updates; pegasus pin is 25 upstream commits behind (latest 2026-10-02), zelda3 current.
- Arch news: mkinitcpio 42 TPM2/LUKS item does not apply (no LUKS here).
- No .pacnew, no failed units, firewall (policy drop) and PipeWire audio paths intact; journal errors only known-harmless hardware messages.
- Orphans left in place: qt5-tools, cmake. Disk 19G/53G used.
- 2026-10-08: installer library: PCGamingWiki lookup by Steam app id for names that don't match a page (unknown DRM 368 -> 125; 40 more DRM-free games found).
- 2026-10-08: Pegasus no longer fullscreen (bar visible); games still fullscreen on top of it.
- 2026-10-08: Risk of Rain runs (OpenSSL 1.0/curl from HLD); installer adds these to old GameMaker games automatically.
- 2026-10-08: sleep hook audio-jack: redo jack detection after hibernation (phantom headphones, no speaker sound).
- 2026-10-08: audio-jack sleep hook removed again: the codec unbind can deadlock in the kernel (rt5645_i2c_remove).

## 2026-10-08 — lid no longer suspends
- `HandleLidSwitch=ignore` in `logind.conf.d/handheld.conf` (was `suspend-then-hibernate`). Power key unchanged.
- Revert: set it back to `suspend-then-hibernate`, `scripts/sync install`, restart systemd-logind (or reboot).
- inputd watches the lid switch: closed → `output * power off`, opened → `power on` (backlight 0 does not turn the panel off). Revert: git revert the commit, `scripts/sync install`, restart inputd.
- Lid closed also disables all input (sway events + pad grab) and runs `dotfiles/sway/lid.sh` (GPU limit to min, statusbar paused). Revert: git revert, `scripts/sync install`, restart inputd.

## 2026-10-08 — idle suspend after 30 min
- swayidle suspend timeout 1200 → 1800 s (`dotfiles/sway/handheld`). Revert: set it back to 1200, restart swayidle.

## 2026-10-08 — Strife: Veteran Edition starts
- Installed `sdl2_net`; symlink `libtheoradec.so.1` in the game dir (Arch has .so.2). Revert: `pacman -Rns sdl2_net`, remove the symlink.
- Strife VE gamepad: config set to the game's Xbox profile (details in frontend.md). Not in the repo.

## 2026-10-08 — Game Saves tool (copy either way)
- Pegasus Utilities > Game Saves (was Save Status): curses UI to compare and copy saves GPD⇄desktop per game, with backups. `desktop/save-status-remote` gained `get`/`put` (restricted key unchanged; it can now write the mapped save files). Needs `git pull` on the desktop. Revert: git revert; the old script only did `status`.

- 2026-10-08: AUR `mangohud-light` updated to 0.8.4-1 (reviewed AUR commit aec701d04109) with local patches 0002-battery-by-type.patch.

## 2026-10-08 — steam-cloud (read Steam Cloud from the GPD)
- New `scripts/steam-cloud` (list/download, read-only) with a private PyPI venv (steam-next 3.0.0) and a refresh token outside the repo. Revert: delete `~/.local/share/gpd/steam-cloud`, git revert.
- Game Saves tool: Steam Cloud as a third source (`c` pulls it to the GPD, read-only; `cloud` names in games/saves.json; `steam-cloud list-many`). Tested with a temp-folder pull of Stardew from the real cloud.
- Game Saves: added SteamWorld Heist, FEZ, Kingdom, Death Road to Canada, Hyper Light Drifter, Risk of Rain, Heretic + Hexen (desktop paths unverified); new `scripts/saves-discover` (Ludusavi manifest + Steam Cloud report).
- Game Saves: `ignore` patterns for folder entries (Stardew steam_autocloud.vdf, HLD gameprefs.dat); desktop paths from the desktop session (ad24aea). Desktop needs a git pull.

- 2026-10-08: AUR `ludusavi-bin` updated to 0.31.0-1 (reviewed AUR commit 9b221f35de61).
- saves-discover now uses ludusavi (ludusavi-bin, AUR, vendored) on both machines instead of own manifest parsing; `save-status-remote discover APPID...`; batched + cached (13 games ~8 s). Removed the repo manifest extract. Desktop needs git pull + ludusavi installed.
- discover: reports steam_synced (files Steam syncs, from remotecache.vdf); saves-discover prefers that desktop copy (Super Meat Boy has two). Super Meat Boy desktop finding in docs/saves.md.
- Psychonauts Profile 2 (the 2026 saves) pulled from Steam Cloud and tracked in Game Saves; steam-cloud download: --only and per-file failures.
- Psychonauts Profile 2 is a Windows profile: Linux pad bindings applied to its .ini (backup .gpd-bak), .ini removed from Game Saves tracking.
- Psychonauts has no profile selector (always Profile 1): cloud profile 2 (2026 saves) copied into the Profile 1 slot, old Profile 1 backed up; Game Saves maps cloud profile 2 -> Profile 1.

## 2026-10-08 — Unepic runs without Steam
- Unepic exited on `SteamAPI_Init()`. Replaced its `lib64/` and `lib32/` `libsteam_api.so` with the gbe_fork emulator (sha256 of the release checked), originals kept as `.orig`; launcher fixed to run `unepic64s`. Recipe in docs/steam-library.md. Revert: copy the `.orig` files back.
- Unepic crashed after loading (persona-name blocklist check got a junk pointer from the emulator): patched that check out of `unepic64s` (3 bytes, backup `.orig`), added `steam_settings/steam_interfaces.txt`. Details in docs/steam-library.md. Revert: copy `unepic64s.orig` back.
- Tried the gbe emulator (libsteam_api and steamclient_loader) on The Binding of Isaac: Rebirth: its binaries are Steam-DRM wrapped (.bind section), does not work. Original libs restored; note in docs/steam-library.md.
- The Binding of Isaac: Rebirth runs: Steam DRM unpacked (steamstub-remover) -> isaac.x64.unpacked, gbe emulator libsteam_api, LD_PRELOAD shim games/shims/xkbshim.c for Xwayland. Recipe in docs/steam-library.md. Revert: restore lib64/lib32 libsteam_api.so.orig, launcher runs isaac.x64.
- Spelunky (Wine) runs: gbe_fork Windows steam_api.dll replaces the game DLL (original .orig), steam_settings added. Recipe in docs/steam-library.md. Revert: copy steam_api.dll.orig back.

- 2026-10-09: AUR `commander-genius-git` updated to 3.6.3.r0.gbee4fcb-1 (reviewed AUR commit 986c16b5ac59).
- Commander Keen: Pegasus entry now runs Commander Genius (AUR commander-genius-git, pinned v3.6.3, vendored) on the installed Steam files via symlinks in ~/.CommanderGenius/games; cgenius.cfg managed (fullscreen 1280x720). Old launcher ran DOSBox under Wine. Revert: restore the `exec wine ./dosbox.exe` launcher (cd base4).
- Commander Genius: 426x240 game area for [Galaxy] and [Vorticon] in cgenius.cfg (x3 = 1278x720 integer scale, fills the panel). Menus show scaling artifacts. Revert: delete those two sections.

## 2026-10-09 — audio: speaker fallback when jack detect is stuck
- Sound gone again (codec reports headphones after an aborted hibernate). Added
  `scripts/audio-speaker` (card profile "HiFi (Mic, Speaker)" + default sink). Diagnosis in
  docs/hardware.md "Audio jack". Tried a 10 s s2idle suspend: no effect.
- Revert: delete the script; `scripts/audio-speaker auto` or reboot restores the normal profile.

## 2026-10-09 — Sam & Max 101 playable: d3d8to9 + DXVK 1.10.3
- Patched `prefs.prop` to 1280x720 (fullscreen with plain wine); the game is D3D8 and ran ~5 fps on
  wined3d (WoW64 GL buffer copies). Added d3d8to9 + DXVK 1.10.3 DLLs to its folder and overrides in
  its launcher: ~39 fps. Details in docs/frontend.md. Revert: remove the three DLLs and override.
- Installed `perf` (official repo, explicit) for profiling; remove with `pacman -Rns perf`.

## 2026-10-09 — display glitch: automatic panel reset in inputd
- `inputd` now watches the DSI DPI FIFO underrun bit (1 s check, lid open only) and power-cycles the
  output like `Mod4+F10`; logs to `~/.cache/gpd/dsi-resets.log`. Needs read access to the GPU register
  BAR: new `system/etc/tmpfiles.d/display-underrun.conf` (`z ... resource0 0440 root wheel`, manifest
  copy). Also installed `intel-gpu-tools` (official repo, for `intel_reg` register reads).
- Root cause still unknown (docs/hardware.md "Display"). Revert: remove the tmpfiles file
  (`sudo rm /etc/tmpfiles.d/display-underrun.conf`, reboot or `chmod 600` the BAR), `git revert` the
  inputd change, `scripts/sync install`, restart inputd.

## 2026-10-09 — audio after hibernate: root cause found, driver patch prepared
- Traced the hibernate/restore of the RT5645 codec (see docs/hardware.md "Audio broken after hibernate"):
  the driver's `regcache_sync()` skips default-valued and volatile registers, so after the power loss the
  jack interrupt (0xbd) and the ASRC (0x8a) stay off. Wrote `kernel/rt5645/` (patch for 7.2.9 + `build.sh`):
  not loaded yet. Manual workaround: reload the sound card (stop PipeWire, modprobe -r/modprobe
  `snd_soc_sst_cht_bsw_rt5645`), tested OK.
- Installed for debugging (official repo): `perf`, `intel-gpu-tools`, `i2c-tools`, `alsa-utils`, `acpica`,
  `linux-headers` (needed to build the module). Remove with `pacman -Rns` when not needed.

## 2026-10-09 — audio after hibernate fixed for good (two kernel driver patches via DKMS)
- Root cause: neither the RT5645 codec driver nor the Intel SST DSP driver restored their hardware after
  hibernation (details and traces in docs/hardware.md "Audio broken after hibernate"). Patches in
  `kernel/` (rt5645: `.restore` handler redoing the probe-time init; intel-sst: map freeze/thaw/poweroff/
  restore to the existing suspend/resume), installed with DKMS (`kernel/install.sh`, package `gpd-audio`
  in `/usr/src`, `dkms` + `linux-headers` from the official repo; the dkms pacman hook rebuilds on kernel
  updates, `/maintain` checks it). Verified over a real hibernate with the DSP active.
- Revert: `kernel/uninstall.sh`, reboot. Packages installed for debugging stay listed in
  docs/packages.md (remove with `pacman -Rns`).
- Lessons: never unload/bind the codec on a running system (CLAUDE.md); `pkill wineserver` leaves orphans.
- Display glitch (shifted picture): separate issue, inputd resets the panel automatically (see above).

## 2026-10-09 — Undertale starts fullscreen
- `games/patches/gm-start-fullscreen.py` sets the start-in-fullscreen flag in `assets/game.unx` (1 byte,
  backup `game.unx.orig`); verified: the window comes up 1280x720 fullscreen at launch. Revert:
  `python3 games/patches/gm-start-fullscreen.py --undo ~/Games/installed/undertale/assets/game.unx`.

## 2026-10-09 — automatic display reset removed again
- The `inputd` DSI-underrun watch reset the panel without a visible glitch (Commander Keen; 9 resets in a day):
  the underrun status bit is a sticky latch, not a "picture broken" signal. Removed the watch and the
  `system/etc/tmpfiles.d/display-underrun.conf` rule (read access to the GPU registers). Manual `Mod4+F10` stays.

## 2026-10-09 — old-style Wine measured: Sam & Max 5 -> 42 fps on Wine's own D3D
- Same Wine version, old-style build (Kron4ek 11.19 `amd64`, in `~/Games/tools/wine-oldstyle`, own prefix
  `~/Games/tools/wineprefix-oldstyle`, outside the repo) vs the system WoW64 build: confirms the WoW64
  buffer-copy cause (docs/frontend.md). Nothing system-wide changed. Packages: `lib32-libunwind`,
  `lib32-libxcomposite` installed (official repo).

## 2026-10-09 — 32-bit Windows games on the old-style Wine in `~/.wine32`
- `~/.wine32` (win32 prefix, created by the old-style Wine) + symlink `~/Games/tools/wine32`; Sam & Max's
  launcher uses them. `scripts/games` (`scripts/gamelib/core.py`) writes launchers with the old-style
  Wine for games whose `.gpd-game.json` has `"runner": "wine32"` (opt-in; Spelunky and Cave Story+ were
  tried on it, no gain, back on the system Wine). The unused test prefix `wineprefix-oldstyle` was deleted.
  Revert: use the system `wine` and `~/.wine` in the launcher; remove `~/.wine32` and `~/Games/tools/wine32`.

## 2026-10-09 — Commander Keen entry fixed
- Its Pegasus description said "(Wine)" although Commander Genius runs it natively. `scripts/games` now honours
  optional `description` and `runner: custom` (never overwritten) in `.gpd-game.json`; Keen's record has both.
  The launcher is unchanged. Visible after the next Pegasus restart (`scripts/restart-pegasus`).

## 2026-10-09 — DSI timeout experiment hung the display (nothing changed on the system)
- Wrote a corrected `MIPI_HS_TX_TIMEOUT` live (docs/hardware.md "Probable root cause"); pipe B stalled
  (`flip_done timed out`) and the machine hard-locked at suspend; hard reset recovered. No file or config
  changed. Lesson: no live writes to DSI registers; test display-driver changes only via a patched module
  loaded at boot.

## 2026-10-09 — DSI timeout theory refuted
- Desync observed with the long (VBT) HS timeout and no modeset since boot: the `u16` truncation of
  `MIPI_HS_TX_TIMEOUT` is not the cause of the desync. See docs/hardware.md. No system change.

## 2026-10-09 — i915 DSI audit, test module recipe, cdclk experiment
- Audit of the i915 DSI/CHV code on the desktop (`kernel/dsi-investigation/FINDINGS.md`): one confirmed bug
  (u16 `MIPI_HS_TX_TIMEOUT`, patch `kernel/i915/0001`, not the desync cause), lead: cdclk 266667 kHz with DSI on
  CHV (upstream fixed the same symptom for Bay Trail only), experiment patch `kernel/i915/0002`.
- `kernel/i915/`: desktop build of a patched `i915.ko` for 7.2.9-arch1-1 (pinned sources), one-shot test boot entry.
- On the GPD: `/boot/initramfs-linux-i915test.img`, `/boot/loader/entries/arch-i915test.conf` (module 0002),
  EFI vars LoaderEntryDefault=arch.conf, one-shot = test entry. Normal initramfs and modules unchanged.
  Revert: `kernel/i915/uninstall-test.sh`.
- 2026-10-09 afternoon: tested i915 patches 0002 (cdclk 320 MHz), 0003/0004 (DPI FIFO flush bit: not present on
  CHV), 0005 (blanking rounding) as one-shot test boots: none prevents the split. Measured with
  `kernel/dsi-investigation/trace-logger.py`: the DSI controller sets the frame timing; each flash was a
  one-frame pipe stall. Stock entry stays default; test entry/initramfs remain (`kernel/i915/uninstall-test.sh`).

## 2026-10-09 evening — patched i915 as the default (DSI split fix)
- Default boot entry `arch-i915test.conf` = patched i915 for 7.2.9-arch1-1 (`kernel/i915/` 0001 HS_TX_TIMEOUT fix,
  0011 DPI underrun logging, 0012 automatic resync after a lone underrun, 0013 start on fastset). Measured: lone
  underruns (which always left a lasting split) were repaired 4/4 with a brief flicker. Boot menu timeout 3 s
  (EFI variable). Revert: `sudo bootctl set-default arch.conf; sudo bootctl set-timeout 0`.
- Pacman hook `/etc/pacman.d/hooks/10-i915test-default-stock.hook` (manifest) resets the default to stock before
  any kernel change, because the test entry only boots with the kernel its initramfs was built for.
- 2026-10-09 late: patched i915 moved to DKMS (`gpd-i915`, `kernel/i915/install-dkms.sh`, rebuilt per kernel on the
  device, 634 s); the one-shot test entry and the `10-i915test-default-stock.hook` are gone. Default entry `arch.conf`
  is patched; new fallback entry `arch-stock.conf` with a stock-module initramfs (`gpd-stock-initramfs`, hook
  `95-gpd-stock-initramfs.hook`, both in manifest). `scripts/update` and `/maintain` warn before kernel updates
  and can hold the kernel. Revert: `kernel/i915/uninstall-dkms.sh`, delete `arch-stock.conf`, `bootctl set-timeout 0`.
- 2026-10-09: boot menu timeout back to 0 (EFI override removed; hold Space at power-on for the menu). New Pegasus
  utility "Reboot (stock display driver)" (`pegasus/utils/tools/stock-boot.sh`, sudoers `22-boot-stock`).
- 2026-10-09: clean boot: kernel `quiet loglevel=3 rd.udev.log_level=3 vt.global_cursor_default=0`,
  silent tty1 autologin (no issue banner, `~/.hushlogin`), plain black sway background.
- 2026-10-09: `RebootWatchdogSec=0` (no watchdog lines on shutdown).
- 2026-10-09: Pegasus utility "Reboot (stock display driver)" and sudoers `22-boot-stock` removed
  (redundant: hold Space at power-on for the boot menu).
- 2026-10-09: installed `ppsspp` 1.20.4 (standalone PSP emulator).
- 2026-10-09: Pegasus PSP collection (`~/Games/psp`, PPSSPP standalone); fetch-boxart knows PSP.
- 2026-10-10: PS1: PCSX ReARMed (buildbot core in `~/.config/retroarch/cores/`), Pegasus collection `~/Games/ps1`.
- 2026-10-10: removed `libretro-beetle-psx` (PS1 runs on PCSX ReARMed).
- 2026-10-10: Sam & Max 101 runs in a Wine virtual desktop (Options menu and Alt+Enter hung in plain fullscreen).
- 2026-10-10: `freeze-games.sh`: Pegasus and the games it started are frozen (SIGSTOP) while the lid is closed or the screen is off on idle.
- 2026-10-10: screen off/on centralized in `dotfiles/sway/screen.sh` (lid and idle screen-off; replaces `freeze-games.sh`); inputd only handles input on lid close.
- 2026-10-10: sleep hook `charger-log` logs PMIC charger registers and bq24190 state before/after sleep (investigating charging stuck at 500 mA after hibernate, power.md). Revert: delete the hook and its manifest line.
- 2026-10-10: lid no longer wakes from sleep (kernel `gpiolib_acpi.ignore_wake=INT33FF:00@35`): closing the lid on a sleeping device woke it and left it awake. Revert: remove the option from `arch.conf`, `scripts/sync install`, reboot.
- 2026-10-10: power button handled by inputd (logind `HandlePowerKey=ignore`): screen on → press sleeps; screen dark → press only turns the screen on (never sleeps); ignored 3 s after a resume (sleep hook `resume-time`). Screen state is asked from sway (IPC GET_OUTPUTS, output `power`). Why: a press meant to wake a screen-off device put it to sleep; a hold can't mean sleep either, since holding ~1 s is what powers it on from hibernation. Revert: logind `HandlePowerKey=suspend-then-hibernate` + `sudo systemctl kill -s HUP systemd-logind`, git revert, `scripts/sync install`, restart inputd.
- 2026-10-10: sleep hook `charger-log` → `pmic-charger`: now also restores the PMIC charger registers firmware resets on hibernate wake (cause of charging stuck at 500 mA, power.md). Revert: delete the hook (and its manifest line), or put back the logging-only version from git.
- 2026-10-10: Pegasus Utils "WiFi" is a menu (status, networks a fresh scan sees, connect, reset Wi-Fi); `/usr/local/bin/wifi-reset` + sudoers `23-wifi-reset` reload iwd and brcmfmac without a reboot (Wi-Fi didn't move to another known network, packages.md). Revert: git revert, delete the sudoers file and script.
- 2026-10-10: `pmic-charger` also rebinds the fusb302 driver after a hibernate wake (firmware resets the USB-C controller too; without it a charger plugged in after hibernate stays at 500 mA).
- 2026-10-10: Pegasus `run` keeps stderr in `~/.cache/pegasus-fe.stderr` and restarts Pegasus after a crash signal (it aborts occasionally when the pad disconnects, frontend.md). Revert: git revert.
- 2026-10-10: shelf (`src/shelf.c`), a lightweight launcher compatible with Pegasus' files (frontend.md). Pegasus stays the default; switch with `~/.config/gpd/frontend`. Installed `sdl3_image`, `sdl3_ttf`. `scripts/sync` build entries take pkg-config packages. Revert: remove the file `~/.config/gpd/frontend`, `scripts/restart-pegasus`.
- 2026-10-10: shelf draws through its own Wayland client with SDL's software renderer (no EGL/Mesa): RSS 171 → 38 MB, first frame ~60 ms.
- 2026-10-10: Utilities "Reboot" and "Shut Down" (`pegasus/utils/tools/{reboot,poweroff}.sh`, passwordless via sudoers 20-power).
- 2026-10-10: no swaybg: sway 1.12 draws uncovered areas black by itself (checked with grim), so the `output * bg #000000` line went and `swaybg_command -` is set (saves a 7 MB process).
