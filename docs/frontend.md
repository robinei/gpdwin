# Pegasus, RetroArch and games

## Pegasus
- Package `pegasus-frontend-stable-git` (AUR, built via `scripts/aur`). Needs `qt5-wayland`
  (no Xwayland here) and `sdl2-compat` (gamepad). Window app_id
  `org.pegasus-frontend.pegasus-fe`, always on workspace 1.
- Started with `~/.config/pegasus-frontend/run`, which prepends `bin/` to PATH and sets
  `QT_BEARER_POLL_TIMEOUT=-1` (Qt's network bearer thread polled Wi-Fi via the old wireless
  extensions API). `bin/dbus-send`
  is a shim: Pegasus asks logind for Suspend/Reboot/PowerOff via dbus-send, which needs polkit.
  The shim maps those to the passwordless sudo rules (Suspend → suspend-then-hibernate) and passes
  everything else to `/usr/bin/dbus-send`.
- Idle redraw: stock Pegasus redrew at 60 fps while showing the menu (~33% CPU, ~2900 irq/s on
  the Atom): every grid tile's loading spinner ran an infinite `RotationAnimator` even when hidden
  (and the splash screen's progress animation kept running after loading). Both are patched in
  our build (aur/patches); now 0 fps, ~2% CPU, ~470 irq/s when idle. Measure with
  `WAYLAND_DEBUG=client` and count `wl_surface...commit()` per second (QSG_RENDER_TIMING crashes it).
  QML gotcha: inside an Animator, `parent` resolves to the enclosing Item's parent, not the Item.
- Gamepad polling: Pegasus polled SDL every 16 ms for its whole lifetime (63 wakeups/s even with
  the screen off or a game running). Patch 0003 polls at 16 ms while there is input and drops to
  100 ms after 5 s without events (first input after a pause can take up to 100 ms).
- Pegasus does not quit when it launches a game; it unloads its theme and waits. It flashes
  briefly while the theme reloads after a game exits.
- Collections: one directory per collection under `~/Games`, each listed in
  `game_dirs.txt`, each with `metadata.pegasus.txt`:
  - `pc` (repo `pegasus/pc`): native games. Pegasus ignores a game without a `file:`, so each
    game has a small launcher script (`diablo.sh`, `hellfire.sh`, `zelda3.sh`). Shortname `pc`
    gives the IBM logo (wanted).
  - `snes` (ROMs, not in the repo; its metadata is a `copy` entry): launch
    `retroarch -f -L /usr/lib/libretro/snes9x_libretro.so "{file.path}"`.
  - `utils` (repo `pegasus/utils`): tools in `tools/*.sh`, 512x512 tiles in `media/`
    (#11111b / #1e1e2e / #45475a / #89b4fa, DejaVu Sans Bold labels). Terminal tools run
    `foot --app-id=pegasus-tool`, fullscreen via a sway rule.
- Don't set `assets.logo` on games: the grid theme then shows the logo instead of the title.
  Box art goes in `assets.boxFront` or `media/<file basename>/boxFront.png` (auto-detected).
- Grid theme shows a console logo for known shortnames (`snes`, `nes`, `gba`, `psx`, `pc`...).
- `scripts/fetch-boxart.py` (linked as `~/Games/fetch-boxart.py`): for ROM folders it knows,
  matches the bare title against thumbnails.libretro.com (prefers USA), saves
  `media/<rom>/boxFront.png`, and rewrites a generated block of `game:`/`file:` lines with clean
  titles at the end of each `metadata.pegasus.txt`. Restart Pegasus afterwards.
- Restart Pegasus with `scripts/restart-pegasus` (it sometimes ignores SIGTERM; the script
  force-kills before starting a new one, so there are never two instances).

## RetroArch
- `retroarch` 1.22 + `retroarch-assets-ozone/xmb`, `libretro-core-info`. Cores (`/usr/lib/libretro`):
  nestopia, snes9x, gambatte, mgba, genesis-plus-gx, picodrive, beetle-pce-fast, beetle-psx
  (needs `scph5500/5501/5502.bin` in `~/.config/retroarch/system/`; none installed).
  PCSX ReARMed (lighter PS1, HLE BIOS) is not in the repos; not installed.
- Config `~/.config/retroarch/retroarch.cfg` (repo copy in `dotfiles/retroarch/`):
  `config_save_on_exit = false` (save from the menu explicitly, then `scripts/sync capture
  retroarch.cfg`).
- Frame pacing (was ~25% dropped frames, 40 fps): `video_driver = gl`, vsync on,
  `video_max_swapchain_images = 2`, `video_fullscreen_x/y = 1280/720`, `audio_driver = pipewire`,
  `audio_latency = 128`, `audio_rate_control_delta = 0.020`, `audio_out_rate = 48000` (no
  resampling in PipeWire). Measured: 0 dropped frames, frame time deviation ~8%, audio
  underruns 7-13% reported (user didn't report crackling). Fallback: swapchain 3 (≈3% drops,
  cleaner audio) or 160-192 ms latency.
- Measure pacing with `statistics_show = "true"` via `--appendconfig` (with
  `config_save_on_exit = "false"`) and a `grim` screenshot; `--max-frames` disables throttling.
- `input_driver = wayland`, auto save/load state on, shader dir `~/.config/retroarch/shaders`
  (Arch's default `/usr/share/libretro/shaders` doesn't exist). Use GLSL shaders with the `gl`
  driver; light ones only (sharp-bilinear, lcd3x, crt-easymode, fakelottes).
- Hotkeys: Guide is hotkey enable. Guide+L2 menu, Guide+L load state, Guide+R save state,
  Guide+R2 (hold) fast-forward, Esc quits. The pad profile's Guide→menu line is commented out in
  `autoconfig/udev/Microsoft X-Box 360 pad.cfg` (otherwise Guide alone opens the menu).
- The full controller profile set was downloaded by the online updater into `autoconfig/udev/`.

## Native games
- DevilutionX (`devilutionx-bin`, AUR): data `diabdat.mpq`, `hellfire.mpq` etc. in
  `~/.local/share/diasurgical/devilution/`. `diablo.ini`: `Sample Rate=48000` (22050 caused
  audio stutter through resampling).
- Zelda 3 (`zelda3-git`, AUR, snesrev/zelda3 built with the user's US v1.0 ROM, sha256 checked).
  `/opt/zelda3-git/zelda3.ini` (original `zelda3.ini.orig` there): `ExtendedAspectRatio =
  extend_y, 16:9` (426x240, exact 3x), Fullscreen, Autosave, ItemSwitchLR, TurnWhileDashing,
  SkipIntroOnKeypress, MiscBugFixes, CancelBirdTravel = 1; `AudioFreq = 48000` (was 44100, which
  PipeWire resampled to 48 kHz).
  `OutputMethod = OpenGL` (was SDL) so GLSL shaders work. `Shader =` takes an absolute path to a
  `.glslp`/`.glsl`; it uses RetroArch's GLSL set (online updater, `~/.config/retroarch/shaders/
  shaders_glsl/`), no separate copy. Current: `handheld/lcd3x.glslp` (made for exact 3x, which this
  config is). Other light picks: `crt/zfast-crt`, `crt/crt-easymode`, `crt/fakelottes`. Too heavy
  here: crt-royale, crt-guest-dr-venom, nnedi3, reshade. Saves in `/opt/zelda3-git/saves`
  (world-writable by the package; files the game creates there are not owned by pacman, so
  uninstalling keeps them). Pad mapping default, untested.
- Psychonauts: GOG Linux installer `~/gog_psychonauts_2.0.0.4.sh` (32-bit native port), not
  installed. Needs multilib + 32-bit libs + probably `xorg-xwayland`. Wine is the fallback
  (needs the Windows installer).

## Game installer (`scripts/games`, Pegasus: Utilities > Games)
- Curses UI over all owned games (`games/steam-library.json`: DRM status and controller support
  from PCGamingWiki, tier = expected performance here, native Linux build, hours). Filters: DRM
  (default DRM-free), tier, controller, installed-only, text search; `s` sorts by hours played.
- Install: DepotDownloader (`-remember-password -validate`, Linux build preferred when one exists)
  into `~/Games/installed/<slug>/`. The user types the Steam password/Guard code; username and
  SteamID are saved in `~/.config/gpd/games.json` (not in the repo). Interrupted downloads resume
  on the next install.
- After download: Steam portrait cover → `gpd-cover.jpg`, executable detection (skips
  uninstallers, redists, crash handlers; prefers the game's own `#!` launch script, which sets
  cwd/env; DepotDownloader drops exec bits, so they are restored; asks when unsure; moves a
  bundled `libSDL2` without Wayland support aside, e.g. Bastion, since there is no X server),
  and warns when a Linux build is 32-bit (no multilib here: use the Windows build via Wine,
  e.g. Hyper Light Drifter), `gpd-launch.sh` (native, or
  `wine` with the shared `~/.wine` prefix and `WINEDEBUG=-all`), record in `.gpd-game.json`.
- `~/Games/installed/metadata.pegasus.txt` is regenerated from the records with
  `collection: PC Games` / `shortname: pc`, so Pegasus merges these games into PC Games
  (verified). `~/Games/installed` is in `game_dirs.txt`. Launchers are not overwritten on
  regeneration (edit them for per-game env vars); "Rewrite launcher" in the UI does.
- On quit after changes it offers to restart Pegasus (Pegasus only rescans on start).
- `R` refreshes the library: Web API key typed each time (never stored), then PCGamingWiki
  lookups. Existing tiers and Linux flags are kept; new games have no tier.
- Code: `scripts/gamelib/` (`core.py` shared, `sources/steam.py`, `tui.py`). Sources are
  pluggable for GOG later (see `sources/__init__.py`).
- DepotDownloader itself: AUR `steamdepotdownloader-bin` (SteamRE release binary, .NET bundled).
  Only games that don't need the Steam client run; PCGamingWiki's "DRM-free" can be wrong.

- Steam API games without the Steam client: games calling `SteamAPI_RestartAppIfNecessary` try to
  start Steam and quit (SteamWorld Heist: exit 255, "steam.sh: No such file"). A
  `steam_appid.txt` with the app id next to the executable skips that; the installer writes it
  when the game ships `libsteam_api.so`/`steam_api(64).dll`. The game then logs
  `SteamAPI_Init() failed` and runs on (achievements/cloud don't work).

- Native Psychonauts (32-bit, SDL 1.2): its own fullscreen switches the display mode, which
  Xwayland's rotated emulated modes break (picture pushed down, mouse offset). Run it windowed at
  1280x720 (`~/.local/share/Psychonauts/DisplaySettings.ini`: `FullScreen=false`); sway makes the
  window fullscreen, which fits exactly. 32-bit MangoHud: `lib32-mangohud` (multilib).
  Mouse clicks landed at scaled coordinates with the bundled original SDL 1.2, so it uses the
  system's `lib32-sdl12-compat` (on lib32-sdl2-compat on SDL3; both AUR): the installer moves
  bundled `libSDL-1.2.so.0`/`libSDL2-2.0.so.0` to `*.bundled` when a system compat lib of the
  same architecture exists (the games' `$ORIGIN` rpath would pick the bundled copy otherwise).
  Camera spin with the pad (PCGamingWiki): in `~/.local/share/Psychonauts/Profiles/*/*.ini` map
  `LookUp/Down_Alt=JoyRotY`, `LookLeft/Right_Alt=JoyRotX` (default uses the triggers' axes).
  Buttons: the defaults assume DirectInput numbering (1=X 2=A 3=B 4=Y); on Linux the pad is
  Joy1=A Joy2=B Joy3=X Joy4=Y Joy5=LB Joy6=RB Joy7=Back Joy8=Start Joy9=Guide Joy10=L3 Joy11=R3,
  and the triggers are axes (the game has no half-axis inputs, so they can't be buttons). Set:
  Jump=Joy1, Attack=Joy3, Cancel=Joy2, Use=Joy4, LockOn/Float=Joy5, PsiPower1=Joy6,
  PsiPower2/3=DPadUp/DPadDown, Journal=Joy8, Stats=Joy7, FirstPerson=Joy11 (all `_Alt`).
  Original profile kept as `Profile 1- Raz.ini.gpd-bak`.
  Runs well and much faster than the Wine version. SDL 1.2 games only look for controllers at
  start: switch the GPD to gamepad mode before launching, and the pad is lost for the rest of the
  session if it disconnects (screen off at the battery idle step, suspend).

- Hyper Light Drifter (native, 32-bit): start via its `run.sh` (bundled OpenSSL 1.0 and a Steam
  runtime curl in `lib/`); the installer now prefers such launcher scripts. Set fullscreen in the
  game's options. It still starts small in a corner: it ignores sway's fullscreen while loading.
  Its `gpd-launch.sh` therefore toggles sway fullscreen off/on 4 s after the window appears (2 s
  was too early; the game sends no events while loading). Manual fix: Super+F twice. The launcher
  is hand-edited: "Rewrite launcher" in the installer would drop this.
  Saves `~/.config/HyperLightDrifter/`. Windows saves did not load in the Linux build (copied
  with lowercase names and mixed-case links: only "New game"), so they were dropped.

- Old GameMaker Linux games (2015-16 runner, 32-bit: Hyper Light Drifter, Risk of Rain) need
  OpenSSL 1.0 and a Steam-runtime libcurl (`CURL_OPENSSL_3`). HLD ships them in `lib/`; a copy
  lives in `~/.local/share/gpd/compat/gamemaker-lib32/` (not in git: third-party binaries), and the
  installer copies them into a game's `lib/` when one of its binaries needs libcrypto.so.1.0.0 and
  the game lacks it (Risk of Rain). These old OpenSSL builds are insecure; the games only use them
  for online features.

## Windows games (Wine)
- System `wine` 11 from the repos (WoW64, no multilib). Plan: one shared prefix (`~/.wine`, the
  default) for everything, to save space; per-game settings go in the game's launcher script
  (env vars such as `WINEDLLOVERRIDES`), not in separate prefixes. Keep winetricks installs to a
  minimum (they affect every game in the prefix).
- No Proton/DXVK. DXVK needs Vulkan; this GPU's Vulkan driver (hasvk) is partial and the GPU is
  weak, so Wine's own D3D→OpenGL (wined3d) is the default. Add DXVK to the prefix only if a game
  needs it, and opt games out per launcher with `WINEDLLOVERRIDES="d3d9,d3d11,dxgi=b"`.
- GE-Proton via umu-launcher was rejected: Steam Linux Runtime + GE-Proton is 1.5 GB+.
- Display: Wine's Wayland driver ignores sway's output rotation and sees the panel's native
  720x1280, so fullscreen games draw into a corner (Hyper Light Drifter; its virtual desktop
  option has no effect with the Wayland driver). `xorg-xwayland` is installed so Wine uses its
  X11 driver, which gets the rotated 1280x720 (sway starts Xwayland only if it is installed when
  sway starts). Verified: Hyper Light Drifter fullscreen fills the screen. Old X11-only native
  games also use it.
- Fullscreen: workspace 1 makes every window fullscreen (docs/desktop.md), but a game set to
  windowed mode in its own settings takes fullscreen straight back (Super+F doesn't stick either).
  Set the game itself to fullscreen. Heretic + Hexen (KEX engine): `v_windowmode "2"` in
  `~/.wine/drive_c/users/robin/Saved Games/Nightdive Studios/Heretic/kexengine.cfg`
  (in game: Options > Video, window mode fullscreen); 1 = fixed 1280x720 window, 0 = resizable
  window, both refuse fullscreen.
- Display modes: Xwayland's emulated resolutions ignore the panel rotation. Wine sees the real
  1280x720 plus portrait modes (480x640, 480x720, 400x640, ...), so there is no 640x480 or
  800x600. Old games that insist on such a mode fail ("needs at least 640x480x32"). Fix: set the
  game itself to 1280x720 in its config before the first start; otherwise a Wine virtual desktop
  (`wine explorer /desktop=game,1280x720 game.exe`) gives Wine's own mode list.
  (The Windows Psychonauts needed `DisplaySettings.ini` in its game folder at 1280x720.)

- Performance overlay: MangoHud, our light rebuild `mangohud-light` (Arch's PKGBUILD vendored in
  `aur/pkgbuilds/mangohud-light`, pkgbuild patch drops mangoplot/mangoapp and with them
  python-matplotlib/numpy and glfw, ~134 MB; `scripts/aur check` says when the repo version moves
  on). Loaded into everything Pegasus starts: `dotfiles/pegasus-frontend/run` exports `MANGOHUD=1`
  (Vulkan layer) and preloads `libMangoHud_shim.so` (OpenGL, incl. Wine/wined3d; 32-bit Windows
  games too, since WoW64 Wine calls OpenGL from 64-bit code). Hidden at start (`no_display`),
  Right Shift+F12 shows it; Pegasus itself is blacklisted. Config `dotfiles/MangoHud/MangoHud.conf`:
  FPS, CPU and GPU clock.
- Psychonauts (Wine, 2026-10-07, replaced by the native build): CPU-bound, ~11 FPS in heavy
  scenes (main thread maxed at 2.4 GHz, wined3d_cs ~85%); mesa_glthread barely helped.
- Every app path must reach PipeWire: PulseAudio clients via `pipewire-pulse` (SDL, OpenAL,
  Wine, `pactl`), ALSA clients via `pipewire-alsa` (`/etc/alsa/conf.d/99-pipewire-default.conf`
  makes ALSA's default device PipeWire). Without `pipewire-alsa`, ALSA apps (e.g. Bastion's FMOD
  Ex) open the busy hardware device, fail silently, and play nothing. Check: `pactl info` says
  "PulseAudio (on PipeWire)", and an ALSA app shows up in `pw-top` as `alsa_playback.<name>`.
- PipeWire + WirePlumber + pipewire-pulse, 48 kHz, quantum 1024-2048: `min-quantum = 1024`
  (`dotfiles/pipewire/...`), because this Atom without RTKit (realtime priority) underran
  constantly at 512 (10.7 ms).
- Speaker hardware buffer: the SST driver came up with 34 x 1008-frame periods (~714 ms), which
  made audio lag (noticed in Bastion). WirePlumber rule `dotfiles/wireplumber/.../51-speaker-latency.conf`
  caps it at 4 periods (driver keeps 1008 frames: 4032 = ~84 ms). No extra underruns seen. If
  crackles appear, raise `api.alsa.period-num` to 6-8. Check: `/proc/asound/card1/pcm0p/sub0/hw_params`.
- OpenAL Soft (`dotfiles/openal/alsoft.conf`, used by e.g. Super Meat Boy): its PipeWire backend
  gave pulsing audio (512-sample periods half-filling the 1024 graph); `drivers = pulse,...` with
  `period_size = 1024` fixed it.
- Rule of thumb when a game's audio stutters or pulses: match the app to 48 kHz, look at `pw-top`
  (ERR column, QUANT of the stream), and check which backend/library the game uses. Speaker sink
  `alsa_output.platform-cht-bsw-rt5645.HiFi__Speaker__sink`. Check xruns with `pw-top` (ERR).
