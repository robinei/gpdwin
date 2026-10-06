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
  PipeWire resampled to 48 kHz). Saves in `/opt/zelda3-git/saves`
  (deleted on uninstall; back up first). Pad mapping default, untested.
- Psychonauts: GOG Linux installer `~/gog_psychonauts_2.0.0.4.sh` (32-bit native port), not
  installed. Needs multilib + 32-bit libs + probably `xorg-xwayland`. Wine is the fallback
  (needs the Windows installer).

## Audio
- PipeWire + WirePlumber + pipewire-pulse, 48 kHz, quantum up to 2048. Speaker sink
  `alsa_output.platform-cht-bsw-rt5645.HiFi__Speaker__sink`. Check xruns with `pw-top` (ERR).
