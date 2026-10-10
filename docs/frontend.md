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
- Crashes: Pegasus sometimes aborts (SIGABRT) when the pad's USB device disconnects or is reset
  (screen off, resume), 3 times in 141 disconnects (Oct 9-10), likely a race in the gamepad
  (SDL) handling; freezing it with the screen off makes it more likely (removal and re-add are
  handled together on thaw). Not reproduced on demand. `run` restarts it after a crash signal
  (not after a normal quit or TERM/KILL; gives up after 3 crashes within 10 s of starting) and
  keeps its stderr in `~/.cache/pegasus-fe.stderr` (last 200 lines + new run): the abort message
  is there for a proper fix.
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
  - `ps1` (bin/cue etc., not in the repo; metadata is a `copy` entry): `shortname: psx`
    (theme logo), launch PCSX ReARMed from `~/.config/retroarch/cores/` (extensions cue, chd,
    pbp, m3u: not bin, which would list each game twice).
  - `psp` (ISOs, not in the repo; metadata is a `copy` entry): standalone PPSSPP,
    `PPSSPPSDL --fullscreen "{file.path}"` (iso/cso/chd/pbp). Name ISOs as in No-Intro
    (`Disgaea - Afternoon of Darkness (USA).iso`) so `fetch-boxart.py` finds the box art.
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
  nestopia, snes9x, gambatte, mgba, genesis-plus-gx, picodrive, beetle-pce-fast.
  PS1: PCSX ReARMed, not in the repos, so the libretro buildbot build
  (`buildbot.libretro.com/nightly/linux/x86_64/latest/pcsx_rearmed_libretro.so.zip`) is in
  `~/.config/retroarch/cores/` (not in the repo; update by hand). BIOS: `PSXONPSP660.BIN` in
  `system/`. Measured on Suikoden II, 3600 frames at 60 fps with the lcd3x
  shader: ReARMed full speed at 52% of one core; Beetle PSX (dynarec) 83% and SwanStation
  (OpenGL 94%, threaded software 88%) both fell below full speed. Unthrottled fps can't be
  compared here (sway paces frames at ~60 even with vsync off), so CPU time is the measure. Beetle PSX
  (`libretro-beetle-psx`) was uninstalled after this; SwanStation was only tested.
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
  `Frame Rate Control=1` is Vertical Sync (v1.5.5: 0 None, 1 Vertical Sync = SDL2 default, 2 Limit FPS).
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
  (default any), tier, controller, installed-only, text search; `s` sorts by hours played.
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
  Optional fields of that record: `"runner": "wine32"` (old-style Wine, see "Wine layout" below),
  `"runner": "custom"` (hand-written launcher the generator never overwrites, even on a forced
  regeneration; used by Commander Keen) and `"description"` (replaces the generated Pegasus
  description, otherwise "Installed from Steam (Wine / native Linux)"). Pegasus reads
  `metadata.pegasus.txt` at start: restart it to see changes.
- `~/Games/installed/metadata.pegasus.txt` is regenerated from the records with
  `collection: PC Games` / `shortname: pc`, so Pegasus merges these games into PC Games
  (verified). `~/Games/installed` is in `game_dirs.txt`. Launchers are not overwritten on
  regeneration (edit them for per-game env vars); "Rewrite launcher" in the UI does.
- On quit after changes it offers to restart Pegasus (Pegasus only rescans on start).
- On start it updates the library (owned games and hours, one Web API request, ~0.5 s); only
  games new to the library get store platforms and PCGamingWiki lookups. Needs the key file
  below and `steamid` in `~/.config/gpd/games.json`; offline it uses the library as it is.
- `R` refreshes the whole library: Web API key from `~/.config/gpd/steam-api-key` (mode 600,
  outside the repo; typed if missing), then PCGamingWiki lookups for every game. Existing tiers
  and Linux flags are kept; new games have no tier.
- The list starts with all games (any DRM, any tier). `?` marks data the library doesn't have:
  no tier yet, no PCGamingWiki page (DRM, controller), platforms or size not looked up.
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
  Original profile kept as `Profile 1- Raz.ini.gpd-bak`. Profile 2 (a Windows profile pulled from Steam Cloud,
  2026-10-08) got the same bindings (CRLF line endings kept; backup `Profile 2- Raz.ini.gpd-bak`).
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

- Strife: Veteran Edition (64-bit native) needs `sdl2_net` (repo) and `libtheoradec.so.1`; Arch ships
  .so.2 with the same `libtheoradec_1.0` symbols, so the game dir has a symlink
  `libtheoradec.so.1 -> /usr/lib/libtheoradec.so.2` (the launcher puts the game dir on
  LD_LIBRARY_PATH). Exit code 127 from Pegasus = missing shared library: run `gpd-launch.sh` in a
  shell and look at the error.
  Gamepad: its first-run config has bogus bindings (e.g. `joyb_fire 29`) and the in-game binding
  screen is unreliable (ignores buttons held/pressed in the first 1.5 s; axes are SDL controller
  indices: 0 LX, 1 LY, 2 RX, 3 RY). Fix: edit `~/.local/share/strife-ve/strife.cfg` and
  `chocolate-strife.cfg` with the game's own Xbox profile (`XInputProfile` in
  `src/strife/fe_gamepad.c` of the bundled source): axes y=1 x=2 strafe=0 look=3; fire=31 (RT),
  use=1, speed=30 (LT), jump=0, prev/next weapon=9/10 (LB/RB), invleft/right=13/14. Button values
  0-14 are SDL controller buttons, 16+ axis-as-button (30 LT, 31 RT). Edit with the game closed
  (it rewrites the files on exit). Not in the repo (game data).

- Undertale (native Linux GameMaker build, `runner` + `assets/game.unx`): the game does not remember
  F4. Fix: set the "Fullscreen" bit of the GEN8 info flags in the data file (`0x9b6` -> `0x9b7`, one byte),
  which makes it start fullscreen (F4 still toggles): `python3 games/patches/gm-start-fullscreen.py
  ~/Games/installed/undertale/assets/game.unx` (idempotent, keeps `game.unx.orig`, `--undo` reverts;
  checked: window 640x480 -> fullscreen 1280x720 with the 4:3 picture letterboxed). It is the same
  idea as the Windows `data.win`/exe mods. Re-run after reinstalling or updating the game. Works for
  other GameMaker games with the same header layout (the script refuses unexpected layouts).

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

- Sam & Max 101 (Telltale, Steamless-unpacked, 2026-10-09). **Final setup: the game's `gpd-launch.sh` runs
  it on the old-style Wine (see the WoW64 measurement below): ~42 fps, smooth, no rendering bugs
  (no invisible characters), no DXVK/d3d8to9 needed.** The DXVK route described next is kept as the
  fallback (`old-launchers/gpd-launch-dxvk.sh`) and for reference; it was needed only because the
  system WoW64 Wine was slow, and it is the one that had the invisible-character bug. Getting the game
  to run at all needed three fixes (1 and the unpacking still apply to the final setup):
  1. Resolution: its `prefs.prop` asked for 800x600, a mode Wine doesn't offer here, so it opened
     a white window and spun on `NtUserChangeDisplaySettings returned -2` (retry every 0.4 s).
     `prefs.prop` is bit-inverted (`~b` per byte); "Fullscreen Size" and "Window Size" are Vector2
     floats. Both replaced by 1280x720 (original `prefs.prop.orig`; idea from WSGF "Telltale Games
     Custom Resolution Tool"). Then plain `wine` is truly fullscreen (no virtual desktop needed;
     `wine explorer /desktop=...` needs the exe's full path, a relative name silently starts nothing).
  2. Speed: the game is **Direct3D 8** (`Direct3DCreate8`) and ran at ~5 fps with Wine's wined3d
     (CPU ~200%, GPU idle). `perf` showed the wined3d_cs thread in a huge memcpy and the game
     thread yielding: the 32-bit game on WoW64 Wine makes Wine copy whole GL buffers on every map
     (`wow64_map_buffer: Doing a copy of a mapped buffer (expect performance issues)`).
     `mesa_glthread` and `vblank_mode=0` changed nothing. DXVK sidesteps it.
  3. DXVK route: plain DXVK d3d9 isn't used by a D3D8 game, so put **d3d8to9** (crosire v1.16.0
     `d3d8.dll`, translates D3D8->D3D9) plus **DXVK 1.10.3** `x32/d3d9.dll` and `dxgi.dll` (last 1.x:
     needs only Vulkan 1.1; hasvk reports 1.2; 2.x needs 1.3 and D3D8 inside DXVK 2.4+ likewise) in
     the game folder, and `WINEDLLOVERRIDES="d3d8,d3d9,dxgi=n,b"` in its launcher. Result: ~39 fps,
     CPU ~45%. Downloads kept in `~/Games/tools/dxvk` (tarball sha256 `8d1a3c91...fd53c6`) and
     `~/Games/tools/d3d8to9` (d3d8.dll sha256 `122928cf...f8ab8`); GitHub publishes no checksums, so
     these are what we got, not verified against upstream. Check it is active with `DXVK_HUD=fps`
     (overlay) and `DXVK_LOG_PATH=dir` (log names "Intel(R) HD Graphics (CHV)", Vulkan 1.2).
     Revert: delete `d3d8.dll`, `d3d9.dll`, `dxgi.dll` from the game folder and the override line.
  Launcher and files live outside the repo (`~/Games/installed/sam-max-101-culture-shock/`).
  Likely applies to other D3D8/D3D9 games that are CPU-bound under wined3d on WoW64 (try d3d9-only
  games with just DXVK 1.10.3).

- Sam & Max 101, Options menu (2026-10-10): opening Options, or Alt+Enter, hung the game in plain
  fullscreen (menu still highlighted on hover and clicked, nothing else; Esc dead). Known for these
  games on modern Windows too: the Options screen resets the display/mode, which fails here.
  **Fix: `gpd-launch.sh` runs it in a Wine virtual desktop** (`wine explorer
  /desktop=samandmax,1280x720 "$PWD/sammax101.exe"`, old-style Wine as before; sway fullscreens the
  desktop window). Options works, no speed loss. The plain launcher is
  `old-launchers/gpd-launch-fullscreen.sh`. The game has no gamepad support (mouse only).

- **WoW64 Wine vs old-style Wine (2026-10-09, measured on Sam & Max, Telltale D3D8, 32-bit):** the system
  `wine` (Arch, new WoW64 build, no 32-bit unix side) ran Wine's OpenGL D3D path at ~5 fps with two
  threads pinned: `perf` showed `wined3d_cs` in a big libc memcpy, the game thread yielding, and the log
  said `wow64_map_buffer: Doing a copy of a mapped buffer (expect performance issues)` and
  `Disabling has_GL_ARB_buffer_storage extension on wow64` (a 32-bit process cannot get a pointer into
  GPU-mapped memory, so every buffer lock is a copy, and persistent mapping is off). **The same Wine
  version built old-style (separate 32-bit and 64-bit parts, uses lib32-mesa, which is installed) ran the
  same path at ~42 fps average (51 at start), no copy warning, no pinned wined3d thread.** Binary used:
  Kron4ek/Wine-Builds `wine-11.19-amd64.tar.xz` (vanilla, `amd64` = both architectures + 32-bit libs;
  `amd64-wow64` is the new style) in `~/Games/tools/wine-oldstyle/`, sha256 `40068b38...bbef3e` (from the
  release notes, `sha256sums.txt` and the API digest), test prefix `~/Games/tools/wineprefix-oldstyle`
  (created with `WINEDLLOVERRIDES="mscoree,mshtml,winegstreamer=d"`; its 32-bit side lacks only optional
  libs: sane, pcsc, pcap, OpenCL, GStreamer, ffmpeg). Sam & Max test launcher:
  `gpd-launch-oldwine.sh` in its folder. Implication: 32-bit D3D games under wined3d suffer on the
  WoW64 system Wine; options are DXVK (D3D9; D3D8 via d3d8to9) or an old-style Wine per game.
  Arch has no old-style `wine` package any more (AUR `wine-stable` builds from source: hours here).

- **Wine layout (2026-10-09):** two Wines side by side. System `wine` (Arch, new WoW64) with the shared
  `~/.wine` prefix for almost everything (the default of `scripts/games`). **Old-style Wine
  (`~/Games/tools/wine32` -> `wine-oldstyle/wine-11.19-amd64`, Kron4ek build) with a true 32-bit
  prefix `~/.wine32` (`WINEARCH=win32`, only the old-style build can make one) for games that are slow on
  the WoW64 system Wine** (so far only Sam & Max). A win32 prefix is half the size of a win64 one (310 vs
  616 MB) with the same speed (39.3 vs 38.6 fps) and Wine memory (449 vs 453 MB). **Opt-in per game:**
  `"runner": "wine32"` in its `.gpd-game.json`; `scripts/games` then writes the launcher with
  `WINEPREFIX=$HOME/.wine32 WINEARCH=win32 WINEDLLOVERRIDES="mscoree,mshtml,winegstreamer=d"` and
  `$HOME/Games/tools/wine32/bin/wine` (Mono, Gecko and GStreamer are not available/needed there; the
  32-bit side lacks only optional libs: sane, pcsc, pcap, OpenCL, GStreamer, ffmpeg). Existing launchers are
  not rewritten (edit them by hand; a game's `old-launchers/` folder keeps the alternatives). Updating the
  old-style Wine: new Kron4ek release -> unpack next to it, repoint the `wine32` symlink, keep the old
  directory until it works (a prefix updates itself on first use).

- **Not every 32-bit game benefits (measured 2026-10-09):** Spelunky (GameMaker, D3D9) and Cave Story+ run at the
  60 fps vsync cap on both the system WoW64 Wine and the old-style Wine, and neither prints the
  `wow64_map_buffer` warning. Only Sam & Max (Telltale engine, big dynamic buffers locked every frame) was
  affected. So use the old-style Wine per game where `WINEDEBUG=err+all,+fps` shows low fps together with
  `wow64_map_buffer`, not as a rule for all 32-bit games.

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

## Commander Keen (Commander Genius)
- The Pegasus entry "Commander Keen Complete Pack" runs Commander Genius (AUR `commander-genius-git`,
  `CGeniusExe`) on the Steam game files in `~/Games/installed/commander-keen-complete-pack/base1..5`
  instead of the DOSBox/Wine versions (that is the Steam pack's `dosbox.exe`).
- CG only looks in `~/.CommanderGenius/games`: `gpd-launch.sh` (hand-edited, kept on metadata regen)
  symlinks `base1..5` there as `Keen 1..5` each launch, then starts CG, which shows its game picker.
- Config `~/.CommanderGenius/cgenius.cfg` is a manifest `copy` (`dotfiles/commandergenius/`; CG rewrites
  it on exit): fullscreen, 1280x720, aspect 4:3 (bars on the 16:9 panel). `dir=PATH` on the command line
  is ignored by this version ("No games detected").
- Widescreen: the game area is 426x240 (x3 = 1278x720, fills the panel at an integer scale; Keen's own 320x200
  would leave borders at x3). Set per engine in `cgenius.cfg`: sections `[Galaxy]` (Keen 4-6) and `[Vorticon]`
  (Keen 1-3), `gameWidth = 426`, `gameHeight = 240`, with `aspect = 16:9` and `integerScaling = true`. The names are
  the engine names, not the game folders (`[Keen4]` is ignored). The values aren't in the game's resolution
  list (320x200, 320x240, 640x360, 640x480), so the settings screen can't pick them and may overwrite them
  if you change the game resolution there. Results: levels are perfect; menus and title pictures (320x200 art
  stretched to 426x240) show scaling artifacts. A fix would be a patch in the engine, not done.
- Tested: menu lists Keen 1-5, fullscreen, Keen 4 plays.

## shelf (lightweight frontend, 2026-10-10)
`src/shelf.c` (C, SDL3 + SDL3_image + SDL3_ttf, sqlite, ICU; built by `scripts/sync install`, manifest
`build` entry with pkg-config packages). It reads and writes Pegasus' own files, so either can be
used at any time without losing anything: game_dirs.txt, the metadata files, `media/`, `stats.db`
(same rows, Pegasus' exact SQL), `favorites.txt` (Pegasus' format), and the grid theme's memory
(`theme_settings/pegasus-theme-grid.json`: both open on the last launched game). The exact Pegasus
rules it follows are in `docs/pegasus-format.md` (from Pegasus' source at the installed commit).
- Switch: `echo shelf > ~/.config/gpd/frontend` (anything else or no file: Pegasus), then
  `scripts/restart-pegasus`. `run` starts whichever is chosen with the same environment (MangoHud
  preload, PATH) and crash restart; MangoHud blacklists both.
- UI: collections as tabs ("Recent", last 20 played games without the Utilities tools, and
  "Favourites" first), a list with small box art on the left, large box art and details (developer,
  year, players, genre, play time, last played, description) on the right. No animations, no
  settings, no power menu (the power button and Utilities cover those).
- Input: pad read directly from evdev in a thread (not SDL's gamepad layer, which polls ~1000/s
  with a joystick open, and whose hotplug path is where Pegasus crashes): d-pad/stick up/down
  (held: repeats), left/right or LB/RB switch tabs, triggers page, A launch, Y favourite.
  Keyboard: arrows, Page Up/Down, Home/End, Tab, Enter, F.
- Draws only when something changed (zero CPU when idle). Its own small Wayland client (xdg-shell
  window, `wl_shm` buffers, keyboard as raw evdev codes while focused; protocol glue generated by
  `wayland-scanner` into `src/vendor/`) with SDL's software renderer drawing into a plain surface.
  SDL's video subsystem isn't used: its Wayland backend loads EGL/Mesa even to show a software
  frame (~145 MB of mapped libLLVM/libgallium). Result: ~38 MB RSS, 11 MB of it its own.
- `run` hands the MangoHud preload to games only (`SHELF_GAME_LD_PRELOAD`), not to shelf.
- Box art is decoded and scaled in a background thread (selected game first, then the visible
  thumbnails); the UI draws at once and art appears as it is ready. Keep images 8-bit: 16-bit
  PNGs took ~100 ms each to convert (the Utilities icons were converted for this).
- Startup on the device (`SHELF_TIMING=1`): first frame on screen ~60 ms after the process starts
  (library load ~4 ms), all visible box art at ~145 ms.
- After a game or tool exits it rescans everything, so games added by the installer or box art
  from Fetch Box Art show up without a restart.
- Testing without a screen: `shelf --list` prints collections, games, art and stats.
  `SHELF_SCREENSHOT=x.png SHELF_KEYS=ddr shelf` renders 1280x720 without any window, presses keys
  (d/u/l/r), waits for the box art, saves a PNG and quits. `XDG_CONFIG_HOME` points it at another config dir.
- Found by name like Pegasus: `screen.sh` (freezing), `inputd` (Guide), the sway workspace-1 rules
  and `restart-pegasus` match `pegasus-fe` or `shelf`.

