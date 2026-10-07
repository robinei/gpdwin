# Pegasus, RetroArch and games

## Pegasus
- Package `pegasus-frontend-stable-git` (AUR, built via `scripts/aur`). Needs `qt5-wayland`
  (no Xwayland here) and `sdl2-compat` (gamepad). Window app_id
  `org.pegasus-frontend.pegasus-fe`, always on workspace 1.
- Started with `~/.config/pegasus-frontend/run`, which prepends `bin/` to PATH. `bin/dbus-send`
  is a shim: Pegasus asks logind for Suspend/Reboot/PowerOff via dbus-send, which needs polkit.
  The shim maps those to the passwordless sudo rules (Suspend → suspend-then-hibernate) and passes
  everything else to `/usr/bin/dbus-send`.
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
- Restart Pegasus after metadata changes: `pkill -x pegasus-fe; swaymsg exec ~/.config/pegasus-frontend/run`.

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
  (deleted on uninstall; back up first). Pad mapping default, untested.
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

## Audio
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
