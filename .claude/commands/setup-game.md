---
description: Set up and tune one installed game on the GPD Win 1 (DRM, renderer, performance) - argument: folder under ~/Games/installed
model: opus
---

Set up the game in `~/Games/installed/$ARGUMENTS` so it starts from shelf and runs as well as this device allows.
The user started this from the game installer (`scripts/games` → `scripts/maintain "/setup-game <folder>"`), so
`sudo -n` works for the session. They hold the device: keep messages short, say before anything appears on the
screen, and let them launch the game from shelf when testing (shelf turns MangoHud on; Ctrl+Shift+F12 shows it).
Ask before changing anything outside the game's folder and launcher. Record what you did and measured in
`docs/steam-library.md` (one entry per game) and `docs/changelog.md`, and commit.

Background (read when relevant): `docs/steam-library.md` (DRM recipes, Dark Souls as the worked example),
`docs/frontend.md` "Game installer", `docs/power.md` "Package power limit", `dxvk/README.md`,
`mesa/reports/R5-softpin-vulkan13.md` (DXVK on this GPU), `scripts/game-wrap` (header).

## 1. What is it
- `.gpd-game.json` (exe, os, runner), the launcher `gpd-launch.sh` (generated; `"runner": "custom"` in the record
  stops the generator from overwriting your edits). Windows exe: 32- or 64-bit (`objdump -f`), runner
  (32-bit → old-style Wine `~/Games/tools/wine32`, prefix `~/.wine32`; 64-bit → system Wine, `~/.wine`).
- Graphics API: `strings -n 6 EXE *.dll | grep -iE '^(d3d8|d3d9|d3d10|d3d11|d3d12|dxgi|opengl32|vulkan-1)\.dll$'`
  (exe and the DLLs next to it; engines load it from a DLL, e.g. UnityPlayer.dll).
- PCGamingWiki page (`pcgw` in `games/steam-library.json`): known fixes, frame-rate caps, physics tied to fps,
  widescreen/resolution mods (Dark Souls: DSfix).

## 2. Make it start (Steam games)
- Steam DRM wrapper: Windows `objdump -h EXE | grep -i bind` → unpack with Steamless (`~/Games/tools/steamless`);
  Linux ELF `.bind` → `~/Games/tools/steamstub-unpack.py`.
- Steam API without the client: `steam_appid.txt` (the installer writes it). If it still exits (silent exit
  code 1 after loading d3d9 was Dark Souls) → gbe emulator: replace `steam_api(64).dll` with
  `~/Games/tools/gbe/w/release/regular/x86|x64/` (keep `.orig`), `steam_settings/steam_appid.txt`,
  `steam_interfaces.txt` from `generate_interfaces_x86|x64.exe` on the original, and for offline play
  `steam_settings/configs.main.ini`: `[main::connectivity]` `offline=1` `disable_networking=1`.
- Test-launch with `swaymsg exec` + a log (`WINEDEBUG=err+all`), screenshot with `grim -s 0.5`; the screen is
  rotated, so crop carefully. Don't leave test windows running.

## 3. Renderer
- **Direct3D 9: DXVK, our build** (`dxvk/`, on the device `~/Games/tools/dxvk/gpd/x32|x64/d3d9.dll`): copy next to
  the exe, `WINEDLLOVERRIDES=...;d3d9=n`, `export hasvk_report_vk_1_3_version=true` (DXVK 2.x needs the claim),
  `export DXVK_GPD_ASYNC_READBACK=1`, and `dxvk.conf` next to the exe with `dxvk.enableAsync = true`,
  `d3d9.numBackBuffers = 3`, `d3d9.maxFrameLatency = 3`. Keep a Wine-d3d variant `gpd-launch-wined3d.sh`
  (`d3d9=b`) and compare. Never 32-bit DXVK >= 2.5 (2.5: GPU hang, 2.6: textures go black).
- Direct3D 8: Wine's d3d8 first; DXVK route (d3d8to9 + DXVK) only if needed (docs/frontend.md "Sam & Max").
- Direct3D 10/11/dxgi, OpenGL, Vulkan: Wine's own (wined3d / the GL driver crocus / hasvk) unless tested
  otherwise. DXVK d3d11 on this GPU is untested: try only with the user's OK, record the result.
- Not Wine's Wayland driver for old games (Dark Souls crashed reading display modes); X11/Xwayland it is.

## 4. Performance: measure, then fix the real limiter
- Baseline in a typical, heavy spot: MangoHud fps (CPU row shows package watts), and in parallel:
  `sudo intel_gpu_top -J -s 1000` (Render/3D busy, clock), `top -H -p PID` (per-thread CPU),
  `/sys/class/powercap/intel-rapl:0/energy_uj` (package power, readable by wheel).
- Locked at exactly 15 or 20 fps: vsync rounding of a fixed 30 fps limiter → turn vsync off
  (`d3d9.presentInterval = 0` in dxvk.conf) and/or a flexible limiter (DSfix `unlockFPS 1`, `FPSlimit 30`).
- Neither GPU (< ~90%) nor any thread saturated → CPU and GPU wait on each other. DXVK HUD
  `DXVK_HUD=fps,submissions,cs,gpuload,drawcalls` (one launch): "Queue syncs" per frame = GPU stalls. Our DXVK's
  wait log (`DXVK_LOG_LEVEL=warn DXVK_LOG_PATH=$HOME/.cache`) names the call, size, format, pool and flags of
  every stall. A small SYSTEMMEM readback (Dark Souls: 16x16 R32F luminance) is covered by
  `DXVK_GPD_ASYNC_READBACK=1`; other patterns may need a DXVK option or a new patch in `dxvk/` (rebuild with
  `dxvk/build.sh` on the desktop, it needs the desktop's sudo).
- Thread waits in detail: `sudo perf sched record -- sleep 8` and attribute the render thread's blocked time to
  its wakers (see the Dark Souls notes). `perf record -g -p PID` for CPU hot spots.
- GPU busy ~100%: fewer pixels (game/mod resolution; 960x540 was the sweet spot for Dark Souls), cheaper effects
  (game options first; MSAA, motion blur, DoF). `INTEL_MEASURE` / `i915-perf-recorder` for per-pass costs.
- Power and fps cap are per-game shelf settings, not launcher lines: in shelf, T CPU turbo, G GPU turbo, W power
  limit (1/2/4/6/8 W, default 6; idle ~1.2 W, SNES in RetroArch ~2.1 W; the firmware's 6 W throttled CPU and GPU together in Dark Souls, 8 W is about the useful
  maximum: CPU 2.4 GHz + GPU 600 MHz draw ~7.9 W, 77-80 C), P GPU clock pin, L fps cap (MangoHud 30/60). Suggest
  values to the user; they press the keys (stored in ~/.config/pegasus-frontend/shelf-game-settings.txt).
- Physics tied to frame rate: keep 30 fps games at 30.

## 5. Finish
- Launcher comments say what each line is for and why; keep fallbacks (other DLLs in a `dxvk-test/` folder).
- Ask the user to play a minute from shelf and confirm (fps, picture, pad, sound, saves). Their saves are backed
  up by game-wrap; check `docs/saves.md` if the game keeps them somewhere unusual.
- Record (steam-library.md entry: what was needed, settings, fps before/after) and commit.
