# Steam library: DRM-free games for this device

Owned Steam games that PCGamingWiki lists as DRM-free on Steam (runnable without the Steam
client), tiered by how likely they are to run on this Atom x7-Z8700 / HD Graphics. Generated
2026-10-07 from the Steam Web API (1353 owned games) + PCGamingWiki page wikitext (991 pages
matched by Steam AppID; 362 games had no confirmed page, so this list is incomplete).

- Tiers are an estimate, not benchmarks. "DRM-free" is PCGamingWiki's claim: verify by running.
- Linux-native vs Windows doesn't matter (Wine is installed); `os` is from the Steam store.
- Controller = PCGamingWiki says it has controller support (full vs partial not known).
- Download: `depotdownloader -app APPID [-os linux|windows] -username NAME -remember-password -dir ~/Games/steam/<name>`
  (the user types their password / Steam Guard code; don't handle Steam credentials).
- Games that exit with "SteamAPI_Init() failed" (PCGamingWiki still said DRM-free, e.g. Unepic 233980):
  they only need a Steam API, not the client. Fix: replace the game's `libsteam_api.so` (64-bit: `lib64/`,
  32-bit: `lib32/`) with the gbe_fork emulator (github.com/Detanup01/gbe_fork, release
  `emu-linux-release.tar.bz2`, `regular/x64|x86/libsteam_api.so`; unpacked in `~/Games/tools/gbe`, not in
  the repo), keep `libsteam_api.so.orig`, add `steam_settings/steam_appid.txt` with the app id. Native
  Linux builds only: Steamless is for SteamStub-wrapped Windows exes, not needed here. Unepic also
  needed `gpd-launch.sh` edited to `exec ./unepic64s` (its `unepic.sh` wanted a missing `unepic64steam`).
  Unepic then crashed after the loading screen: the game checks `SteamFriends()` persona-name history
  against a blocklist and gbe returns a junk pointer (0x64) into `strcmp`. Fix: `31 c0 c3`
  (`xor eax,eax; ret`) at file offset 0xe0b60 of `unepic64s` (original: `unepic64s.orig`); also put the
  game's interface list in `steam_settings/steam_interfaces.txt` (`tools/generate_interfaces` on the
  `.orig` lib).
  **The native emulator reads `steam_interfaces.txt` from the directory of the `.so` (`lib64/`), not from
  `steam_settings/`** (shown with an LD_PRELOAD file-access log: `./lib64/steam_interfaces.txt`). Without it,
  old-SDK games that call the legacy accessors (`SteamRemoteStorage()`) get the newest interface version
  with a different vtable: Unepic's `FileExists` always answered "no", so it never read its saved
  `config.cfg` back (age/language screens and the "new dialog file" question on every start, pad
  disabled because `padEnabled` stayed at its default) while writing it fine. Fix: `lib64/steam_interfaces.txt`
  as a symlink to `../steam_settings/steam_interfaces.txt`. Do the same for every native game with this
  emulator (Isaac has the file only in `steam_settings/`, not yet checked).
  Unepic (233980) was removed again on 2026-10-10 after a long attempt to get its native Linux build usable
  with the gamepad (the Windows build under Wine is the fallback; untested). What was learned, in case
  another game with the same engine or a similar problem turns up: the game reads the raw SDL joystick API
  once per frame into one table of "pressed" flags (slots 0-3 D-pad hat as left, right, bit 2, bit 0; then two
  per axis; then the buttons from slot 16: A=16 B=17 X=18 Y=19 LB=20 RB=21 Back=22 Start=23 Guide=24 L3=25
  R3=26 on an Xbox 360 pad); each action (`padMap_N` in its cloud `config.cfg`, value = slot + 1) names one
  slot. The defaults assume another pad layout (Jump on Back, Pause on A, Target on a right-stick direction,
  triggers unbound), the controller dialog shows PlayStation icons and takes only Back, Start, LT and RT, and
  there is no pad-type detection (it never asks Steam about the controller and ignores the axis/button/hat
  counts). `games/shims/padshim.c` (LD_PRELOAD) fixes the generic problems: Xbox triggers rest at -32768 (the
  game saw them as pressed and ignored the pad; rescaled to 0..32767), D-pad up/down swapped
  (`PADSHIM_SWAP_HAT_UD=1`), axis order (`PADSHIM_XINPUT_LAYOUT=1`). Editing the seven `padMap_` values in the
  config would have fixed the bindings, but pad polling then failed to start in some launches (a field the
  poll function checks was 0 instead of 20-25; not understood, not tied to shelf or the config) and
  the attempt was dropped. Method that worked: a small LD_PRELOAD logger for the SDL joystick calls and
  `sudo gdb -p` with breakpoints/watchpoints (no PIE: addresses are fixed; `ptrace_scope` is 1, so attaching
  needs sudo).
  Launch the game from shelf when testing: shelf ignores the pad only for games it started itself, otherwise it reacts to pad presses (it launched
  other games while Unepic was in front). Found with gdb: break on the fault, read the stack/registers, disassemble the caller.
  Games wrapped in Valve's Steam DRM (a `.bind` ELF section, SteamStub; test with `readelf -SW BINARY |
  grep .bind`), e.g. The Binding of Isaac: Rebirth (250900): first unpack the 64-bit binary with
  github.com/rroohhh/steamstub-remover (`unpack.py`, ~150 lines, reviewed; needs pyelftools +
  pycryptodome in a throwaway venv; writes `BINARY.unpacked`, copy in `~/Games/tools/steamstub-unpack.py`).
  Steamless is Windows PE only. Then the gbe `libsteam_api.so` + `steam_settings/` as above. Without the
  unpack, gbe's `steamclient_loader` only reaches "Application load error 3:0000067431".
  Isaac's static old GLFW then segfaults in `XkbGetKeyboard()` (returns NULL under Xwayland);
  `games/shims/xkbshim.c` (LD_PRELOAD, builds the description from XkbGetMap + XkbGetNames) fixes it;
  built to `~/Games/tools/shim/xkbshim.so` with `gcc -shared -fPIC -o xkbshim.so xkbshim.c -lX11`.
  Isaac's launcher `gpd-launch.sh` runs `isaac.x64.unpacked` with that preload.
  Windows games under Wine (e.g. Spelunky 239350, "Error initializing Steam API"): same idea with the
  Windows release `emu-win-release-vs22.7z` (7z; sha256 checked): `regular/x86/steam_api.dll` (32-bit) or
  `regular/x64/steam_api64.dll` replaces the game's DLL (original kept as `.orig`), `steam_settings/` with
  the app id and `steam_interfaces.txt` (`wine tools/generate_interfaces/generate_interfaces_x86.exe
  steam_api.dll.orig` run inside `steam_settings/`). Unpacked in `~/Games/tools/gbe/w`.
  Dark Souls: Prepare to Die Edition (211420, 2026-10-12): installed by `scripts/games` (Windows, old-style
  Wine `wine32`), `DATA/DARKSOULS.exe` has no SteamStub; with the original `steam_api.dll` it loaded d3d9 and
  exited with status 1 silently; with gbe x86 `steam_api.dll` + `steam_settings/` (app id, interfaces from the
  original) it runs (Wine d3d9 -> wined3d/OpenGL), ~15 fps in game. Now DXVK 1.10.3 (`DATA/d3d9.dll` x32,
  `d3d9=n`): works, 30 fps (the game's cap) at the title screen. DXVK 2.6.2 (last release that the R5 review
  expects to work with the claim-1.3 driconf, `hasvk_report_vk_1_3_version=true` as an env var, verified with
  `vkq`: api 1.3.354): device created (log lists Vulkan 1.3 features), but the picture is black and of its HUD
  only the frame-time graph (untextured lines) draws, not the text: textured draws fail. No GPU hang during
  that run. Wined3d launcher kept as `gpd-launch-wined3d.sh`; test kit in the game's `dxvk-test/` (README).
  Not yet known whether all of 2.x fails (2.0 would tell) or what in hasvk breaks (sampling/descriptors?).
  In game DXVK 1.10.3 showed 15.0 fps (frames 56-92 ms): GPU 3D 75% busy at ~445 of 600 MHz, no CPU thread
  saturated (game render thread 49%, main 25%, dxvk-cs 22%); pinning the GPU at 600 MHz changed nothing.
  So frames take ~55 ms and vsync rounds them to 66.7 ms. DSfix 2.4 (Nexus, user-downloaded; zip sha256
  `9566ef64...bedd1`) in `DATA/`: `DINPUT8.dll` (launchers add `dinput8=n,b`), `DSfixKeys.ini`, `dsfix/`,
  `DSfix.ini` (original as `DSfix.ini.orig`) changed only in: renderWidth/Height 1920x1080 -> 1280x720
  (game's own is 1024x720), dofOverrideResolution 540 -> 0 and dofBlurAmount 1 -> 0 (game's original DoF),
  borderlessFullscreen 0 -> 1 (needs the in-game fullscreen setting off); enableVsync is already 0.
  Still exactly 14.9 fps after that (also in a cheap close-up; GPU ~70% busy at ~420 MHz, no thread
  saturated): a cap, not load. Suspects: vsync still on (DSfix lists enableVsync under "not yet ready") or the
  game's own 30 fps limiter waiting for fixed 33 ms ticks. Set together (not yet tested): `DATA/dxvk.conf`
  `d3d9.presentInterval = 0`, DSfix `unlockFPS 1` + `FPSlimit 30` (variable frame rate, capped at the original 30).
  Also in DSfix.ini: `skipIntro 1`, `disableCursor 1`, `forceWindowed 1` (with borderless; the in-game fullscreen
  setting is off anyway). DSfix keys: Backspace toggles its 30 fps limit, RCtrl HUD, F6-F8 cursor/borderless.
  Result: 18 fps at 1280x720 (so 15 was the fixed-tick rounding); now GPU-bound (3D 90% busy, ~570 of 600 MHz;
  threads 49/31/17%). Then render 960x540 and `dofOverrideResolution 180`: DSfix has no DoF off switch, but its
  source (`getDofRes`, d3d9dev.cpp) takes any height: 180 makes the DoF targets 320x180 and 160x90 instead of the
  game's 512x360 and 256x180 (a quarter of the pixels, blurrier DoF). Untested values per the ini; back to 0 if
  DoF looks broken. User: "better". Wined3d with the same DSfix settings (and `vblank_mode=0`): worse, so DXVK 1.10.3
  stays (`gpd-launch.sh`; wined3d: `gpd-launch-wined3d.sh`). One whole-device freeze (10:40, right after starting the
  game; no log at all, nmi_watchdog off): cause unknown.
  DXVK 2.x: the black screen is 32-bit DXVK >= 2.5 on hasvk (2.5.3 GPU hang, 2.6.x textures lost after a while),
  mesa/reports/R5 "Narrowed down"; 2.0 and 2.4 work. Game switched to DXVK 2.4 (claim-1.3 env var in the launcher);
  1.10.3 kept in `dxvk-test/`. Then dxvk-gplasync v2.4.1-1 (gitlab.com/Ph42oN/dxvk-gplasync `releases/`, sha256 `ee96eaf6...f8a1f4`, no
  upstream checksum; 32-bit d3d9tex: correct after 2400 frames, no hang) with `dxvk.enableAsync = true` in dxvk.conf
  (async pipeline compiles; hasvk has no graphics pipeline library, so only the async part applies).
  Profile 2026-10-12 (gplasync 2.4.1, DSfix 960x540, ~20-24 fps; method: `INTEL_MEASURE=renderpass,file=...,
  control=FIFO` in the launcher, `echo 60 > FIFO`; `perf record -g -p`; `i915-perf-recorder -m RenderBasic|
  RenderPipeProfile` + `i915-perf-reader -c all`):
  - GPU-bound: per frame 41.8 ms GPU span, 35.8 ms in draws, 5.6 ms gaps; ~714 draws (658 indexed). CPU: no hotspot,
    ~1.6 of 4 cores busy (game code 54% of samples, dxvk-cs + hasvk ~17%).
  - By shader: one shader pair 205 draws/frame (5.7 ms, likely shadow/depth), a few heavy geometry shaders
    (0.3-0.5 ms per draw); full-screen passes (<=6 vertices) 8.5 ms/frame (24%), single post passes 0.6-1.4 ms.
  - Counters (one ~31 ms window): GPU busy ~100% at 485-580 MHz; EU active 54-60%, EU stall 28%; samplers 70% busy,
    23% texel misses; ~3.3 GB/s read + 2.1 GB/s write; rasterized ~15M pixels per 31 ms (many times 960x540:
    shadow maps, overdraw, post); early-depth bottleneck 34%, early-depth fails ~28% of pixels, **HiZ fails 0**
    (hierarchical depth apparently not in use).
  - HiZ in hasvk (Mesa 26.2.4 source): enabled on gen8 for single-sample, single-mip depth attachments, sampling
    with HiZ allowed on gen8 (`anv_can_sample_with_hiz`); off for multi-mip ("Enable multi-LOD HiZ"), gen8 MSAA
    ("Enable gfx8 multisampled HiZ"), GENERAL-type layouts with input-attachment usage (silent), and resolved away
    in layouts without aux (silent). `INTEL_DEBUG=perf` run: only "Enable gfx8 multisampled HiZ" (one image). Not
    the swapchain: `d3d9.forceSwapchainMSAA = 0` kept the warning, HiZ fails still 0, no fps change (reverted).
    DSfix passes the game's MSAA type through unchanged. Open: which image is MSAA, why the main depth gets no HiZ
    (a silent path, or the counter doesn't count on CHV).
  - Cause found and fixed: hasvk had no HiZ at all since Mesa 23.0 (mesa/0006-hasvk-restore-hiz.patch, Mesa 1.7,
    installed). In game HiZ now rejects most hidden pixels (early-depth bottleneck 34% -> 14%), GPU load ~90% ->
    ~76%, but fps about the same: ~19-24 depending on the spot.
  - Where the frame goes now (perf sched, 8 s): the game's render thread HighGraphics runs ~50-66% (game code, ~30 ms
    per frame), and is blocked ~25-40%, woken by DXVK's dxvk-queue (frames finishing); GPU ~76% busy at ~460 MHz.
    GPU pinned at 600 MHz: no change (memory-bandwidth bound). `d3d9.numBackBuffers = 3` + `d3d9.maxFrameLatency = 3`
    kept (dxvk.conf; no measurable change, no added lag noticed); maxFrameLatency 6: no change.
  - Wine's Wayland driver (launcher with `unset DISPLAY`): the game crashes at start (page fault reading 0 in
    darksouls+0xb70225 while handling display modes, stack holds 1280x720/800x450/... sizes); back to X11.
  - **The real limiter (found 2026-10-12)**: DXVK HUD `submissions`: 1 queue sync per frame, ~19 ms. A DXVK build with
    a wait log (repo `dxvk/`) showed it: every frame `LockRect` READONLY on a 16x16 R32F SYSTEMMEM surface (the
    eye-adaptation luminance, copied with GetRenderTargetData), so the CPU waited for the whole GPU frame; CPU and
    GPU took turns. `DXVK_GPD_ASYNC_READBACK=1` (our patch) skips that wait (values a frame or two old):
    **30 fps (the cap)**, GPU 93-96% busy at ~580 MHz. The game now runs our `dxvk/build.sh` d3d9.dll; the release
    gplasync and 1.10.3 DLLs stay in `dxvk-test/`. Two whole-device freezes with no log during DXVK sessions
    (10:40 stock driver, 12:32 with HiZ): cause unknown; `ssh ... sudo dmesg -w` to the desktop catches the next one.
  - Then: DSfix `FPSlimit 60` tried and set back to 30 (the game's cap; DSfix warns of physics glitches above 30), and the
    launcher pins the GPU minimum clock to its current maximum while the game runs (`rps_min_freq_mhz`, made
    wheel-writable in `/etc/tmpfiles.d/turbo.conf`), back to RPn afterwards.
  - Power: the firmware sets the package power limit (RAPL PL1) to 6.0 W (1 s window; PL2 20 W / 10 s). With the GPU
    fully loaded the package sat at 6.07 W, the GPU fell to ~460 MHz despite the minimum-clock pin and the CPU
    clocked down (seen at 1.76 GHz); temperatures 62-71 C. PL1 8 W: 7.5-7.9 W drawn, GPU 600 MHz, CPU 2.4 GHz,
    **24.3 -> 28.0 fps** in a heavy area; temperatures level at 77-80 C after 2 min (limit ~90 C). The launcher sets
    8 W while the game runs (now `GPD_TDP_W=8 GPD_GPU_PIN=1` for game-wrap, docs/power.md); a reboot resets it too. Offline: `DATA/steam_settings/configs.main.ini` (gbe) `[main::connectivity]` `offline=1`,
  `disable_networking=1`; in game also Settings > Network settings > Launch setting "Start offline". Also: GPU hangs (stopped heartbeat) happened with wined3d runs too (09:51, 09:53, 10:43).

## Very likely runs well, controller support

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| Super Meat Boy | 40800 | linux | 6.0 |
| VVVVVV | 70300 | linux | 0.0 |
| Gish | 9500 | windows | 2.5 |
| They Bleed Pixels | 211260 | linux | 0.0 |
| SteamWorld Dig | 252410 | linux | 0.0 |
| Salt and Sanctuary | 283640 | linux | 1.9 |
| Blasphemous | 774361 | linux | 0.5 |
| Hyper Light Drifter | 257850 | linux | 0.0 |
| Titan Souls | 297130 | windows | 0.0 |
| Broforce | 274190 | linux | 0.0 |
| Hotline Miami | 219150 | linux | 0.2 |
| Hotline Miami 2: Wrong Number | 274170 | linux | 0.0 |
| Risk of Rain (2013) | 248820 | linux | 0.2 |
| Monaco | 113020 | linux | 0.1 |
| Party Hard | 356570 | linux | 0.0 |
| Hue | 383270 | linux | 0.0 |
| Splasher | 446840 | linux | 0.0 |
| Cave Story+ | 200900 | windows | 0.0 |
| Bastion | 107100 | linux | 9.1 |
| Transistor | 237930 | linux | 0.6 |
| Undertale | 391540 | linux | 0.0 |
| Stardew Valley | 413150 | linux | 2.1 |
| Darkest Dungeon® | 262060 | linux | 0.0 |
| SteamWorld Heist | 322190 | linux | 0.1 |
| Death Road to Canada | 252610 | linux | 0.0 |
| Moon Hunters | 320040 | linux | 0.0 |
| Hammerwatch | 239070 | linux | 0.3 |
| Atom Zombie Smasher | 55040 | linux | 3.1 |
| Cthulhu Saves the World | 107310 | windows | 0.5 |
| Breath of Death VII | 107300 | windows | 0.0 |
| A Bird Story | 327410 | linux | 0.0 |
| Beholder | 475550 | linux | 0.0 |
| Fran Bow | 362680 | linux | 0.0 |
| Kingdom: Classic | 368230 | linux | 0.0 |
| Kingdom: New Lands | 496300 | linux | 0.2 |
| Day of the Tentacle Remastered | 388210 | linux | 0.0 |
| Full Throttle Remastered | 228360 | linux | 0.0 |
| Grim Fandango Remastered | 316790 | linux | 1.8 |
| Psychonauts | 3830 | linux | 3.6 |
| Quake | 2310 | windows | 7.4 |
| Heretic: Shadow of the Serpent Riders | 2390 | windows | 0.0 |
| Hexen: Beyond Heretic | 2360 | windows | 0.0 |
| Heretic + Hexen | 3286930 | windows | 0.3 |
| Wolfenstein 3D | 2270 | windows | 0.0 |
| Strife: Veteran Edition | 317040 | linux | 0.5 |
| Serious Sam Classic: The First Encounter | 41050 | windows | 0.0 |
| Serious Sam Classic: The Second Encounter | 41060 | windows | 0.0 |
| SiN Gold | 1313 | windows | 0.6 |
| Thief Gold | 211600 | windows | 0.0 |
| Thief™ II: The Metal Age | 211740 | windows | 0.0 |
| Red Faction II | 20550 | windows | 0.0 |
| Mafia | 40990 | windows | 0.0 |

## Maybe: test at low settings

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| Hollow Knight | 367520 | linux | 0.0 |
| A Short Hike | 1055540 | linux | 0.0 |
| Pyre | 462770 | linux | 0.0 |
| Rayman Origins | 207490 | windows | 0.0 |
| Kentucky Route Zero | 231200 | linux | 3.2 |
| Half-Life 2 | 220 | linux | 25.1 |
| Half-Life: Source | 280 | linux | 0.0 |
| Portal | 400 | linux | 5.4 |
| Prince of Persia: The Two Thrones | 13530 | windows | 0.0 |
| Hitman: Contracts | 247430 | windows | 0.0 |
| Amnesia: The Dark Descent | 57300 | linux | 0.5 |
| GRIS | 683320 | windows | 0.0 |
| The Swapper | 231160 | windows | 0.0 |
| Disco Elysium | 632470 | windows | 0.5 |
| Valkyria Chronicles™ | 294860 | windows | 0.0 |

## Light, but mouse/keyboard driven (GPD mouse mode or thumb keyboard)

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| FTL: Faster Than Light | 212680 | linux | 0.9 |
| Papers, Please | 239030 | linux | 0.0 |
| Baldur's Gate: Enhanced Edition | 228280 | linux | 50.8 |
| Baldur's Gate II: Enhanced Edition | 257350 | linux | 55.6 |
| Legend of Grimrock | 207170 | linux | 0.0 |
| The Blackwell Legacy | 80330 | linux | 0.0 |
| Blackwell Unbound | 80340 | linux | 0.0 |
| Blackwell Convergence | 80350 | linux | 0.0 |
| Blackwell Deception | 80360 | linux | 0.0 |
| Primordia | 227000 | linux | 0.0 |
| Ben There, Dan That! | 37420 | windows | 0.6 |
| Time Gentlemen, Please! | 37400 | windows | 0.0 |
| Fallout | 38400 | windows | 0.0 |
| X-COM: UFO Defense | 7760 | windows | 0.1 |
| X-COM: Terror from the Deep | 7650 | windows | 0.0 |
| Deus Ex: Game of the Year Edition | 6910 | windows | 22.3 |
| Arx Fatalis | 1700 | windows | 0.0 |
| Dungeon Siege | 39190 | windows | 2.3 |
| The Binding of Isaac | 113200 | windows | 0.7 |
| Crayon Physics Deluxe | 26900 | windows | 0.0 |
| Samorost 2 | 40720 | windows | 0.0 |
| Space Rangers HD: A War Apart | 214730 | windows | 5.0 |
| Roadwarden | 1155970 | linux | 0.0 |
| Nihilumbra | 252670 | linux | 0.0 |
| Stronghold HD | 40950 | windows | 0.0 |

## Probably too heavy, or not judged

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| Age of Wonders III | 226840 | linux | 2.7 |
| AI War: Fleet Command | 40400 | linux | 1.5 |
| Alpha Protocol | 34010 | windows | 20.7 |
| Amnesia: A Machine for Pigs | 239200 | linux | 2.0 |
| Among the Sleep | 250620 | linux | 0.0 |
| Antichamber | 219890 | linux | 0.0 |
| ARK: Survival Evolved | 346110 | windows | 0.0 |
| Arma: Cold War Assault Remastered | 65790 | linux | 0.2 |
| Assassin's Creed | 15100 | windows | 6.5 |
| ATOM RPG Trudograd | 1139940 | linux | 0.0 |
| Audiosurf | 12900 | windows | 0.0 |
| Baldur's Gate 3 | 1086940 | windows | 4.1 |
| Balrum | 424250 | linux | 0.3 |
| Barotrauma | 602960 | linux | 0.0 |
| Batman™: Arkham Knight | 208650 | windows | 0.0 |
| Battlefield: Bad Company™ 2 | 24960 | windows | 1.1 |
| Beat Cop | 461950 | linux | 0.0 |
| Besiege | 346010 | linux | 0.0 |
| Bionic Dues | 238910 | linux | 0.2 |
| Bloodstained: Ritual of the Night | 692850 | windows | 0.0 |
| Breathedge | 738520 | windows | 0.0 |
| Brütal Legend | 225260 | linux | 0.0 |
| Call of Juarez | 3020 | windows | 0.0 |
| Caves of Qud | 333640 | linux | 0.4 |
| Command & Conquer™: Renegade | 2229890 | windows | 0.4 |
| Cortex Command | 209670 | windows | 0.0 |
| Cruelty Squad | 1388770 | windows | 0.0 |
| Crusader Kings II | 203770 | linux | 1.4 |
| Crysis | 17300 | windows | 7.8 |
| Crysis Warhead | 17330 | windows | 8.2 |
| Darksiders Genesis | 710920 | windows | 0.0 |
| Darksiders II Deathinitive Edition | 388410 | windows | 1.2 |
| Darkwood | 274520 | linux | 0.0 |
| Divinity II: Developer's Cut | 219780 | windows | 0.0 |
| Divinity: Original Sin (Classic) | 230230 | windows | 1.6 |
| Divinity: Original Sin 2 | 435150 | windows | 1.4 |
| Dungeon Siege 2 | 39200 | windows | 0.2 |
| Eldritch | 252630 | linux | 0.5 |
| Europa Universalis IV | 236850 | linux | 0.0 |
| Exanima | 362490 | windows | 0.3 |
| Expeditions: Viking | 445190 | windows | 0.0 |
| F.E.A.R.: Perseus Mandate | 21120 | windows | 0.0 |
| Far Cry 2 | 19900 | windows | 46.6 |
| Firewatch | 383870 | linux | 5.8 |
| Galactic Civilizations II: Ultimate Edition | 202200 | windows | 0.0 |
| Gorky 17 | 253920 | linux | 0.0 |
| Grand Theft Auto: San Andreas | 12120 | windows | 0.8 |
| Hades | 1145360 | windows | 0.5 |
| In Sound Mind | 1119980 | windows | 0.0 |
| Kerbal Space Program | 220200 | linux | 0.1 |
| Noita | 881100 | windows | 0.1 |
| Nosferatu: The Wrath of Malachi | 283290 | windows | 0.0 |
| Obduction | 306760 | windows | 0.0 |
| Orcs Must Die! 3 | 1522820 | windows | 0.0 |
| Ori and the Blind Forest | 261570 | windows | 0.0 |
| Ori and the Blind Forest: Definitive Edition | 387290 | windows | 3.6 |
| Ori and the Will of the Wisps | 1057090 | windows | 0.0 |
| Outlast | 238320 | linux | 0.0 |
| Overgrowth | 25000 | linux | 0.0 |
| Painkiller: Redemption | 65560 | windows | 0.0 |
| Pathologic Classic HD | 384110 | windows | 0.0 |
| Phantom Fury | 1733240 | windows | 0.0 |
| Pillars of Eternity | 291650 | linux | 37.1 |
| Pillars of Eternity II: Deadfire | 560130 | linux | 0.0 |
| PixelJunk™ Eden | 105800 | windows | 0.0 |
| Planetary Annihilation | 233250 | windows | 0.0 |
| Poly Bridge | 367450 | linux | 0.0 |
| Psychonauts 2 | 607080 | linux | 0.0 |
| Quake 4 | 2210 | windows | 0.0 |
| Rain World | 312520 | windows | 0.0 |
| Return of the Obra Dinn | 653530 | windows | 0.4 |
| Rise of the Triad: Ludicrous Edition | 1421490 | windows | 0.2 |
| Scanner Sombre | 475190 | windows | 0.0 |
| Shadow Tactics: Blades of the Shogun | 418240 | linux | 0.0 |
| Shadowrun Returns | 234650 | linux | 12.7 |
| Shadowrun: Dragonfall - Director's Cut | 300550 | linux | 20.1 |
| Sir, You Are Being Hunted | 242880 | linux | 0.4 |
| Sniper Elite | 3700 | windows | 0.0 |
| SOMA | 282140 | linux | 18.4 |
| Stellaris | 281990 | linux | 0.0 |
| Stories: The Path of Destinies | 439190 | windows | 0.0 |
| Styx: Master of Shadows | 242640 | windows | 9.6 |
| Styx: Shards of Darkness | 355790 | windows | 0.0 |
| Subnautica | 264710 | windows | 0.0 |
| Sunless Sea | 304650 | linux | 1.7 |
| System Shock 2: 25th Anniversary Remaster | 866570 | windows | 1.0 |
| System Shock: Enhanced Edition | 410710 | windows | 2.5 |
| Terminator: Resistance | 954740 | windows | 3.6 |
| The Ascent | 979690 | windows | 0.0 |
| The Book of Unwritten Tales | 215160 | linux | 0.2 |
| The Cave | 221810 | linux | 0.2 |
| The Fall | 290770 | linux | 0.0 |
| The Last Campfire | 990630 | windows | 0.0 |
| The Silent Age | 352520 | windows | 0.0 |
| The Vanishing of Ethan Carter Redux | 400430 | windows | 6.2 |
| The Void | 37000 | windows | 0.0 |
| The Witness | 210970 | windows | 6.6 |
| Thirty Flights of Loving | 214700 | linux | 0.0 |
| Tower of Time | 617480 | linux | 0.0 |
| Tyranny | 362960 | linux | 0.0 |
| Underworld Ascendant | 692840 | linux | 0.0 |
| Valley | 378610 | linux | 0.0 |
| Wasteland 2 | 240760 | linux | 0.0 |
| Wasteland 3 | 719040 | linux | 0.0 |
