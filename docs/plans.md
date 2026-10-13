# Plans

Features being designed, before they are built. Each section: the goal, the design so far, open questions.
When one is built, its description moves to the topic doc (`docs/desktop.md`, `docs/frontend.md`, ...), the
change gets a `docs/changelog.md` entry, and the section here is removed. Unsorted ideas stay in
`docs/ideas.md`.

## Rust and a project structure for the system program

**Goal:** write the system program ("One system program", below) in Rust as a Cargo workspace, not as more single C files. The C
programs (shelf, inputd, statusbar; ~5200 lines) keep running until it replaces them.

**Why:** a long-running program that owns input, power and the screen, with threads sharing one state, parsing
untrusted files (Pegasus metadata, images, Steam data). Rust gives memory and thread safety there, `cargo test`
for the parts that are pure logic, and a structure that can grow.

**Design so far:**
- A workspace in its own folder (name to pick; not `system/`, which is the repo's system files):
  - `core`: the state and its rules (idle, lid, modes, focus), no I/O, unit-tested;
  - `policy`: evdev (`evdev` crate), logind inhibitor locks and sleep (`zbus`), sway IPC, uinput, sysfs
    (power, backlight, charger), device.conf;
  - `library`: Pegasus format, `stats.db` (`rusqlite`), the user layer's games (docs/pegasus-format.md is the
    spec; the C shelf the reference implementation);
  - `ui`: Wayland surfaces (`smithay-client-toolkit`: xdg toplevel, layer-shell, virtual keyboard), drawing
    (CPU: `tiny-skia` + a text crate; or GPU, if the Atom's Mesa startup cost allows), the shelf, companion
    and keyboard views;
  - one binary.
- Port the behaviour, not the code: the C sources and docs are the spec; their tests are what was measured on
  the GPD (first frame ~40 ms after start, docs/frontend.md).
- **No panics by construction**: workspace lints deny `unwrap`/`expect`/`panic!`/slice indexing
  (`clippy::unwrap_used`, `expect_used`, `panic`, `indexing_slicing`) outside tests; errors are values,
  handled where they occur (a bad file or a missing device degrades one feature, never the program); no
  `unsafe` outside small reviewed FFI spots. Backstops for what lints can't catch (a dependency panicking,
  a deadlock): the UI runs in its own thread, a panic there is caught (`catch_unwind`) and the UI restarted
  while the input thread keeps forwarding the pad; locks recover from poisoning; a systemd watchdog
  (`WatchdogSec`, pinged by the input thread) restarts a hung program; `Restart=always` last.
- Programs that work on their own stay small and separate if they fit no better inside (statusbar, scripts).

**Development before the Thor arrives (~a month):** on the desktop, nested sway with two outputs sized like
the Thor's panels (`WLR_WL_OUTPUTS=2`), headless sway + `grim` for automated checks, a uinput virtual pad,
sysfs under a configurable root with fake trees for lid, battery and charger, unit tests for `core`. Needs
`sway grim foot` on the desktop (not installed yet; planning only for now).

**Open questions:**
- **Building on the device** (less pressing now: the GPD unit is probably retired, 2026-10-13, and the Thor
  and the Ally build Rust fine). `scripts/sync` compiles the C programs on the device in seconds. A Rust release
  build on the GPD's Atom (4 GB) would take a long time and may run out of memory. Options: build on the
  device anyway where it is fast (the Thor), cross-compile on a desktop and copy the binary (sync kind
  `cargo`, building where `cargo` is fast), or prebuilt binaries per architecture (x86_64, aarch64) from a
  release. Decide before porting.
- Drawing, decided 2026-10-13: own Wayland surfaces (`smithay-client-toolkit`), no SDL; CPU rendering into
  shm buffers to start (sway still composites them on the GPU). Why CPU: no GPU context of our own that
  competes with the game while the bar and second screen keep updating, nothing to lose when a game hangs or
  resets the GPU, instant start, small dependencies; the second screen redraws little (the bar once a
  second, damage regions only). Open: shelf's full-screen view on the main screen (1080x1920, 120 Hz,
  scrolling, box art) may be too much for the CPU at full rate: measure on the desktop with the Thor's sizes;
  if it is, draw that view with the GPU (Vulkan/GLES through Turnip, or `wgpu`) behind the same drawing
  interface, and keep the second screen on the CPU.
- Repository, decided 2026-10-13: a new repo for everything (see "New repository" below).
- Order: build the new program against the new device (the Thor) first, where the second screen and the pad
  layer are needed. The C programs stay for devices/gpd-win1 (other GPD owners) until the new one covers them.

## Window model: sway without tiling (Thor)

**Goal:** sway for its capabilities (wlroots, Wayland protocols, IPC, outputs, input), never as a tiling
window manager: every app is full screen, one at a time on the main screen, switched from the second screen.
No tiling, borders, title bars or workspace names ever visible.

**Design so far:**
- **One hidden workspace per app on the main screen.** When a window opens (sway IPC `window::new`), the
  system program moves it to its own workspace (named by the window, never shown) and makes it fullscreen.
  Switching apps = switching workspace. Each workspace holds exactly one app, so sway's one-fullscreen-per-
  workspace rule never bites (on the GPD all apps shared workspace 1 and inputd re-fullscreened whichever
  got focus, docs/desktop.md "fullscreen stack"; this replaces that). Fullscreen also gives direct scanout.
- Dialogs and popups (transient/floating windows) stay on their parent's workspace and float centred over it
  (`popup_during_fullscreen smart`).
- shelf is one of these apps (its own workspace), the one shown when nothing else is.
- Closing an app: its workspace disappears; the previous app (most recently used) comes back, else shelf.
- No sway key bindings for tiling, layouts, splits or workspaces at all; `default_border none`, no swaybar.
  Everything the user does goes through the system program.
- **Second screen**: one workspace, normally empty under the program's layer-shell views; an emulator's
  second window goes there ("Second screen" below).
- **App switcher**: a second-screen mode (and a pad-layer binding: D-pad left/right = previous/next app,
  most recently used order): the open apps as a list or grid with icon and title, tap to switch, a close
  button (polite close, then kill after a timeout for hung games). Thumbnails later, if sway can capture
  single windows (`ext-image-copy-capture` with toplevel sources; check the sway version).

**Open questions:**
- Apps that open several top-level windows (Steam, some emulators' settings windows, Wine launchers): one
  workspace per window, or group by app (`app_id`/pid) with the newest shown. Probably group by app, with the
  app's windows as tabs in the switcher.
- Games that resize or leave fullscreen themselves: re-assert fullscreen, or respect it.
- Xwayland windows without proper transient hints that should be dialogs.
- Compositor choice (2026-10-13): sway, for now. ROCKNIX runs sway on the Thor (both panels, touch, rotation
  tested on this hardware); it has every protocol needed (layer-shell, input-method-v2, virtual pointer and
  keyboard, tearing control, direct scanout) and IPC enough to impose this model. Alternatives considered:
  gamescope (games only, one output, no layer-shell: wrong for two screens), cage (one app), river 0.4 (an
  external window manager over a protocol: our program could be the window manager; young, check its
  Wayland protocol support), our own compositor on Smithay (full control, but a compositor crash kills every
  app, and a big project). Keep the window-model code in one module so moving to river or our own compositor
  later stays possible.

## Steam in shelf (Thor)

**Goal:** shelf lists and starts Steam games next to everything else, with Steam doing what it does best
(downloads, Proton, cloud saves, achievements, DRM), but never in the way: no Steam UI unless asked, not
running when nothing needs it. On the GPD shelf avoided the client (DepotDownloader, gbe emulator,
`scripts/games`); on the Thor the client is affordable (16 GB) and Valve's ARM work (FEX, Proton) assumes it.

**Design so far:**
- Library: read Steam's own files, no API: `libraryfolders.vdf` and `appmanifest_*.acf` (installed games,
  size, last played), `librarycache/` art for box art. Only installed games; a "Steam" collection in shelf.
- Launch: start Steam in the background on demand (`steam -silent`), wait until it is logged in, then
  `steam -applaunch <appid>`; shelf tracks the game by its `steam_app_<appid>` window class (sway) and
  Steam's reaper process, for play time and for "game running" state. Steam shuts down (`steam -shutdown`)
  some minutes after the last Steam game exits, unless kept.
- Installing and uninstalling: in Steam itself (decided 2026-10-13). shelf has an entry that opens Steam's UI
  (Big Picture, or the library) for that; shelf lists only installed games, and picks up changes from the
  manifests.
- Windows: sway rules put `steam_app_*` (games) fullscreen on the main screen. **Steam's own windows stay
  hidden** unless asked for: `steam -silent` starts without a window, and a sway rule moves every other Steam
  window (class `steam`: the client, updater, dialogs) to the scratchpad, where it exists but is not shown.
  - Shown on request: shelf's Steam entry brings the client (or Big Picture) out of the scratchpad onto the
    main screen; leaving it sends it back.
  - Windows that need an answer (login or Steam Guard, cloud save conflict, launch options, EULA, an error)
    are not lost: the system program sees each new Steam window over sway IPC and shows a notice with its
    title on the second screen; a tap (or the system button) brings it up. Which titles count as "needs an
    answer" and which are just progress (updates, "Preparing to launch", shader pre-caching) is learnt on the
    device; progress can show as a line in the bar instead.
  - The in-game overlay is part of the game window and unaffected.
- Input: Steam Input sees our virtual Xbox pad ("Owning the input path"); keep Steam Input off for it by
  default (our layer and profiles do that job), on per game if a game needs Steam's remapping.
- Bottom screen during a Steam game: the same follow mode (controls, per-game settings, bar).

**Open questions:**
- How Steam runs on the Thor: the x86 client under FEX (what Pocknix does, with `pocknix-steamos-shim`), or
  an ARM64 client if Valve releases one beyond the Steam Frame. Measure its idle cost under FEX
  (`steam -silent`: RAM, CPU, battery).
- Proton on ARM: Steam's own Proton under FEX, or ARM builds (Pocknix ships `proton-cachyos`/`proton-ge`
  with DXVK); whether Steam can be told to use them.
- Non-Steam games through Steam (for its overlay or Steam Input): not wanted by default.
- Which Steam UI the shelf entry opens: Big Picture (pad-friendly) or the desktop client.

## One system program

**Scope (2026-10-13): a program for the AYN Thor.** Built for its two screens, its pad and buttons, its
sysfs paths; no generality for other devices or single-screen fallbacks. Anything device-specific stays in
one module so a later device is a port, not a design constraint now.

**Goal:** one program instead of inputd, shelf, the companion and the on-screen keyboard: one in-memory state
(focus, selected game, layer and override modes, lid, idle, power) that every part sees at once, with no IPC
or state files between them. It has 0, 1 or 2 windows: none while a game has the screen (only the input and
power policy run), shelf on the main screen, the companion or keyboard on the second.

**Design so far:**
- Wayland client with up to two surfaces: shelf's window (xdg toplevel, so sway rules still place it) and
  layer-shell surfaces for the second screen, the keyboard and popups. The keyboard types through
  `zwp_virtual_keyboard_v1` (as wvkbd does), so no separate keyboard program.
- Crash isolation, since a bug in drawing must not take the input and power policy with it:
  - the policy (evdev, lid, idle, power button, charger, pad grab) runs in its own thread with no SDL or image
    code, and owns the state; the UI thread reads it under a lock and sends it commands;
  - **lid and power button fall back to logind**: instead of `HandleLidSwitch=ignore` /
    `HandlePowerKey=ignore` (system/etc/systemd/logind.conf.d/handheld.conf), the program holds logind
    inhibitor locks (`handle-lid-switch`, `handle-power-key`); if it dies the locks go and logind suspends or
    hibernates as configured;
  - a systemd user service with `Restart=always`; state that must survive a restart (the last selection,
    modes) is saved as now;
  - games run in their own scope (`systemd-run --user --scope` or `setsid` with a pid file), so a crash or
    restart never kills a running game, and the restarted program finds it again.
- Memory: while a game runs, shelf's textures and fonts are freed or kept small (the game needs the RAM on a
  4 GB device).
- statusbar: built in (see "Second screen": the bar on the second screen); swaybar is then not needed.

**Open questions:**
- Migration: grow shelf into it (it has the window and the state), then move inputd's policy in as a thread;
  keep inputd buildable until the merged program has run for a while.
- Whether SDL3 can give the layer-shell surfaces, or those use plain Wayland next to SDL's window (shelf
  already uses Wayland protocols directly, `src/vendor/`).
- Does a sway or GPU reset (DSI resync, Super+F10) disturb the surfaces; the policy thread must not care.

## Owning the input path

**Goal:** all gamepad input passes through the system program, so it can transform it: a mouse mode (sticks
move the pointer, triggers click, for desktop apps and mouse-only games), the system layer, remaps and
per-game profiles, deadzones and calibration, chords. The pad system layer becomes one use of this.

**Design so far:**
- The program grabs the real pad for good (`EVIOCGRAB`) and creates a **virtual pad** through uinput that games
  see instead. In normal mode events are copied straight through (same thread, no queue: well under 1 ms);
  in other modes they become pointer, keyboard or system actions, or are remapped first.
- The virtual pad looks like a well-known controller (Xbox 360/One vendor and product ids, standard button and
  axis codes), so SDL and Wine games map it without configuration whatever the device's own pad is.
- Mouse and keyboard output: sway's virtual-pointer and virtual-keyboard protocols where a Wayland client is
  enough; a uinput mouse/keyboard where something outside Wayland needs it (Xwayland games read them anyway).
- Force feedback: games upload rumble effects to the virtual pad; the program forwards them to the real one
  (uinput FF upload/erase callbacks, as InputPlumber does). Needed for rumble to keep working.
- Per-game profiles: shelf already starts the games, so it knows which profile applies.
- Safety, as for the system program: if the program dies the grab ends and the real pad works directly again.
- **Hide the real pad**: a grab only stops its events; the device stays visible, so games would list a silent
  real pad next to the virtual one (and may pick it as player 1). A udev rule makes the real pad's event node
  accessible only to the program (its group) and drops `ID_INPUT_JOYSTICK` for everyone else; the game
  launcher also sets SDL's ignore list (`SDL_GAMECONTROLLER_IGNORE_DEVICES`, SDL3's equivalent) with its
  vendor/product id. Then games, Wine and Steam see exactly one pad: the virtual one. If the program is not
  running, the real pad is hidden too: the service must be up for any pad input (acceptable: it restarts).

**Open questions:**
- Use **InputPlumber** (Rust, ShadowBlip; ChimeraOS/Bazzite; Pocknix and Thorch ship Thor maps for it) and
  drive it over D-Bus (it has profiles and an "intercept" mode for exactly a system button), or do it in the
  system program. Own: one process, one state, no D-Bus, minimal; InputPlumber: device maps, FF and edge cases
  already solved. Read its Thor config either way.
- Games reading the pad through hidraw (SDL HIDAPI, Steam Input) see the real pad and bypass the grab: hide
  the real pad's hidraw node (udev: root-only) once we own the path. The Thor's pad is a UART MCU (`rsinput`),
  so it probably has no hidraw node at all; Bluetooth/USB pads would.
- Permissions: `/dev/uinput` and the event devices for the program's user (udev rule, `input` group).
- Gyro, if a device has one (the Thor may: check), as mouse or stick input in the mouse mode.

## Pad system layer (inputd)

**Goal:** control the system without a keyboard: workspaces, terminal, on-screen keyboard, windows, power. The Thor
has no keyboard. Started as `docs/ideas.md` item 15.1.

**Design so far:**
- One of the Thor's extra buttons (Home or Back) is the **system button**. A tap does what the Menu key does now (shelf);
  **holding** it turns on the layer.
- While the layer is on, inputd **grabs the pad** (`EVIOCGRAB`, as it already does with the lid closed), so the
  game sees none of these presses. Releasing the system button ungrabs it.
- The bindings are in a config file (`~/.config/gpd/pad-layer.conf` or similar: button = sway command or a
  built-in action), so they change without a rebuild. A first set:

  | Button | Action |
  |---|---|
  | Y | on-screen keyboard on/off (`wvkbd`; on a second screen, there) |
  | A | terminal (foot) |
  | B | close the focused window |
  | X | shelf |
  | D-pad left / right | previous / next workspace |
  | D-pad up / down | focus the other output (dual screen) |
  | right stick, R1 / L1 | mouse pointer and clicks (virtual-pointer protocol, as the touchpad mode) |
  | left stick | scroll |
  | L2 / R2 | brightness (or volume) down / up |
  | Start | power menu: suspend, hibernate, power off |
  | Select | screenshot |

- Feedback while the button is held: a small overlay listing the bindings (on a second screen if there is one,
  else a swaybar line or a layer-shell popup).
- Emulators: shoulder buttons could send the emulator's save/load state keys when an emulator has the focus
  (ideas.md 15.1).

**Open questions:**
- Which button: Home or Back (check what the `rsinput` driver reports for each).
- Tap versus hold: a hold threshold (~300 ms?) delays the tap action; or tap = shelf only on release without
  another button pressed.
- Release must be robust: whatever happens (crash, button lost on suspend), the pad is ungrabbed. The kernel
  drops the grab when inputd exits; inputd itself must not keep the layer on after resume or a pad reconnect.
- Games reading the pad through hidraw (SDL HIDAPI, Steam) bypass the grab and would see the presses. Check
  which games here do.
- The uinput mouse: a separate small device, or part of a general "pad as desktop" mode.
- On-screen keyboard: `wvkbd` (layer-shell, wlroots) is the candidate; size and position per device
  (bottom screen on a dual-screen device, bottom half otherwise). It types through the virtual-keyboard
  protocol, which sway supports.

## Second screen (dual-screen devices, e.g. AYN Thor)

**Goal:** the second screen follows what is on the main screen, with override modes on demand. It is never a
second desktop to manage by hand.

**Design so far:**
- The Thor's outputs: main `DSI-2` (top, rotated), second `DSI-1` (bottom, touch).
- A small **companion** program owns the second screen (a layer-shell surface on that output, like a panel).
  It follows sway's focus (IPC window events) and shelf's state, and draws what fits:

  | On the main screen | Second screen (follow mode) |
  |---|---|
  | shelf | the selected game: art, description, play time, last played, its per-game settings (touch to change) |
  | a game started from shelf | its info: controls, per-game settings, maybe a small performance line; dark otherwise |
  | an emulator with two screens (DS, 3DS, Wii U) | the emulator's second window, moved there by sway rules; the companion stands aside |
  | a terminal or desktop app | dark, or workspace and window list |

- **Override modes**, chosen from the pad system layer (one button cycles them, or a binding each) or by touch:
  - keyboard: `wvkbd` on the second screen, typing into the main screen's focused window;
  - sysinfo: clock, battery and charge rate, temperatures, CPU/GPU clock and power limit, Wi-Fi (the status
    line's data, larger, maybe with short graphs);
  - pad layer: while the system button is held, its bindings (see "Pad system layer");
  - touchpad: the second screen as a trackpad for the main one. The program gets the touches on its own
    surface (`wl_touch`) and moves the pointer through sway's virtual-pointer protocol
    (`zwlr_virtual_pointer_v1`; no uinput or root needed). Gestures: one finger moves (with acceleration), tap
    = left click, two-finger tap = right click, two fingers scroll, tap-and-drag = drag, three-finger swipe =
    workspace. Maybe an absolute mode too (the second screen maps onto the main one) and a strip of buttons
    or keys along an edge. Combines with the keyboard mode (keyboard below, touchpad above) for desktop use.
  - off: the panel powered down (`output … power off`).
  - launcher: a grid of apps and tools (desktop entries, shelf's Utilities, terminal, settings), started
    on the main screen by touch or the pad; recent and pinned first.
  An override lasts until it is dismissed, then follow mode returns.
- **Built-in status bar** on the second screen, replacing swaybar + statusbar: a strip along one edge of the
  second screen, always visible over every mode, except when an emulator's second window takes the screen
  (then it hides, or moves to the main screen only if asked). Its data comes from the program's own state
  (clock, battery, power, temperatures, Wi-Fi, volume), so no separate status program and no i3bar protocol.
  The main screen stays free of bars.
- **Switching modes**:
  - touch: the status bar is the mode switcher, always on screen: a tap on its mode icons (follow,
    launcher, keyboard, touchpad, sysinfo) switches; tapping the current mode's icon returns to follow;
  - pad: the system button + a button (Y keyboard, ...; see "Pad system layer"); holding the system button
    also shows the bar and the bindings when an emulator has the second screen and the bar is hidden;
  - automatic: the keyboard appears when a text field gets focus and goes when it loses it: the program is
    an input method (`zwp_input_method_v2`, which sway supports with `text-input-v3` clients; GTK, Qt, SDL3
    use it). Apps without text-input still need the manual toggle;
  - when focus on the main screen changes, an override stays (it was asked for), except automatic keyboard.
- Touch on the second screen goes only to the companion or the second window (`map_to_output`), so a touch
  never lands in the game by accident.
- Power: OLED black costs little, but powering the panel off when it shows nothing saves more. Measure both.

**Open questions:**
- How shelf shares its state: the selected game and its settings (a state file it already writes,
  `~/.local/state/shelf/last`, or a small socket/IPC). Or shelf draws the second screen itself as a second
  window while it has the focus, and the companion only handles the rest.
- One program for the companion and the override modes, or the companion plus `wvkbd` as a separate client.
- Which game info is worth showing (controls per emulator system? notes per game in the user layer?).
- Idle: inputd dims and turns off both outputs; the second one could go dark sooner.

## New repository (decided 2026-10-13)

**Goal:** a fresh repo for the AYN Thor (the ROG Ally stays on Windows, 2026-10-13): the system program and the
setup around it. This repo becomes the GPD Win 1 archive: public, still usable by GPD owners, no longer
worked on. Fresh history (none of this repo's personal data or old paths); MIT for our work as here.

**Structure (proposal):**
- the system program: a Cargo workspace (`core`, `policy`, `library`, `ui`; "Rust and a project structure"
  above) at the root or in one folder, with an Arch PKGBUILD for x86_64 and aarch64;
- `components/`, `devices/`, `users/example/` and `scripts/sync` + `layers.py`: the layer setup as here
  (docs/repo-layout.md), with the system program as a component replacing `inputd` and `shelf`;
- the user layer in a private repo (`user_dir=`), from the start;
- `docs/`, `CLAUDE.md`, `.claude/commands/` (install, port-device, maintain, setup-game, aur-add).

**Carry over (adapted, not copied blindly):**
- the layer design: `scripts/sync` (link/copy/keep/build/dkms, templates, drift check), `scripts/layers.py`,
  `src/devconf.h`'s device.conf keys (as a Rust module), packages.txt/services.txt;
- the rules in CLAUDE.md: changelog per change, ask before risky steps, licence checks, AUR review
  (`scripts/aur`), no secrets in the repo;
- the playbooks, made device-neutral (install from the install medium; on the Thor an SD image instead:
  ROCKNIX ABL, Arch Linux ARM);
- `components/base` and `desktop` configs that are not GPD-specific (sway, foot, fish, PipeWire, iwd,
  nftables, sudoers, journald, sysctl); `pacman.conf` per architecture (Arch Linux ARM has no multilib);
- game tooling as an optional component: `scripts/games` + `gamelib`, saves backup and sync, `fetch-boxart`,
  Steam library; check what still applies on aarch64 (FEX for x86 games, native Linux builds need arm64);
- `docs/pegasus-format.md` (the spec for `library`), docs/frontend.md and docs/desktop.md as the behaviour
  spec for the new program; the C sources as reference only.

**System management**: superseded 2026-10-14 by "Packages, built on the device" below (was: `sync` as on
the GPD; darch considered and set aside). Still worth having: btrfs snapshots around pacman transactions.

**Packages, built on the device (decided 2026-10-14).** Supersedes "System management ... as on the GPD": the
repo's own files reach the system only as packages, and the Thor builds them itself (fast, 1 TB), no desktop
in the loop.
- Our packages: the system program; `thor-config` (sway config, PipeWire, udev, sleep hooks, systemd units
  and presets; defaults in `/usr/share` and `/etc/xdg`, so the home directory starts empty); `thor-base` (meta
  package). The device packages (kernel, firmware, bootloader, BSP) come from Pocknix's repo, not ours.
- **The loop**: edit in the checkout on the device, then one command (`scripts/pkg <name>`, or all changed
  ones) runs `makepkg` and `pacman -U`. A local repo on the device (`repo-add`) holds every build, so
  `pacman -Syu` and downgrades see them. Versions from git (`pkgver` = describe + commits, plus a build number
  for uncommitted builds) so each build is an upgrade. No dev overrides: always build and install. Rust
  builds are incremental (`cargo` target dir kept between builds).
- **The agent runs on the Thor too** (Claude Code has linux-arm64 builds): checkout, builds, installs and tests
  all local, no ssh. Started from a terminal on the device, from shelf's Utilities (as the GPD's Maintenance
  entry), or reached remotely (ssh, or a remote session from the desktop or phone) when typing on the device
  is too slow. Guard rails, as CLAUDE.md's rules: a narrow sudoers rule for what the loop needs
  (`pacman -U` from the local repo, `systemctl` for our units), everything else asks; a btrfs snapshot before
  every pacman transaction; kernel, bootloader and firmware changes only with the user's yes (a bad one
  can leave the device unbootable, and the agent with it); never restart the system program or sway while a
  game runs.
- Drift: `pacman -Qkk` for files changed under the packages; `/etc` edits go into the package, not the device.
- **Publishing**, only when ready and only our own packages: sign with our GPG key and upload to a public repo
  (GitHub Releases/Pages); others add it to `pacman.conf` next to ALARM and Pocknix's repo. Later, an image
  script (pacstrap ALARM + Pocknix + ours into a disk image with the ROCKNIX boot partition, swap, btrfs,
  first-boot setup) for others to flash; check the terms for the Qualcomm firmware in it before publishing.

**Other Thor projects (checked 2026-10-14), to learn from:**
- **Kettle Linux** (github.com/kettlelinux/kettlelinux, BSD-3, very detailed docs): SteamOS "deckard"
  (Steam Frame) aarch64 userspace with **Valve's native arm64 Steam client**, mainline 7.2 + patch stack,
  RAUC A/B updates. Its Thor findings (docs/THOR.md, BOOT.md, BOOTLOADER.md):
  - **Boot without flashing ABL**: AYN's stock ABL fastboot menu can default to **"Loader"**, which starts
    AYN's U-Boot from `loader_a/b` (an Android boot image), which offers EFI: `\EFI\BOOT\BOOTAA64.EFI` from
    the SD card or internal storage, then GRUB (or systemd-boot) and the kernel. Needs an unlocked
    bootloader. `loader_*` is recoverable from fastboot; only `abl_*` needs EDL. Their Thor runs with both
    ABL slots stock. If the menu lacks "Loader" (older firmware?), ROCKNIX ABL v1.2 also starts
    `BOOTAA64.EFI` first. **This changes our plan: prefer Loader mode, no ABL flash; a normal EFI boot
    (systemd-boot, kernel + initramfs on an ESP) instead of mkbootimg images.**
  - Connectors: top `DSI-2` (1080x1920, touch axes swapped: a libinput calibration matrix fixes it),
    bottom `DSI-1`; bottom backlight renamed `bottom-panel` so tools pick the top one first.
  - Thor's U-Boot reports DMI "AYN Odin 2"; audio card `ayn-AYNOdin2` under EFI.
  - Bottom screen costs ~0.7 W at idle; the panel lists only 60 Hz, 30 Hz possible at half pixel clock
    (60 Hz 1.55 W, 30 Hz 1.45 W with a still image).
  - Suspend: 5-cycle s2idle soak fine (pad, touch, panel, Wi-Fi).
  - In Game Mode it leases the bottom connector to a second gamescope with KWin/Plasma Mobile: heavy;
    our sway setup drives both outputs directly.
- **Armada** (github.com/armada-os/armada, ~1900 stars, GPL-2.0+): Fedora bootc image, ROCKNIX-derived device
  support, ARM64 Steam + FEX + Proton. The biggest Thor community (Discord).
- **Ratatoskr** (github.com/venatrix-ritz/Ratatoskr, MIT): Armada/Decky plugin making the bottom screen a
  trackpad, keyboard (uinput), split, quick settings; grabs the bottom digitizer. Reference for our modes;
  notes Steam dims only the top panel.
- **Gleipnir** (github.com/venatrix-ritz/Gleipnir, MIT/GPL-2.0+): charge limit on the Thor by writing 0 to
  `/sys/class/power_supply/battery/constant_charge_current` at 80 % and restoring it at 77 % (verified on one
  Thor). The charger has no end threshold; our device layer can do the same.

**Stays here (the archive):** `devices/gpd-win1` (kernel patches, Mesa, DXVK, docs), the C programs
(`src/`), the GPD-specific dotfiles and scripts (speaker tuning, audio snapshots), `docs/changelog.md`, the
history. The GPD's driver patches could still be sent upstream from here.

**Open questions:**
- Name of the new repo and the program.
- Whether the device layers live in the new repo or each in its own (one repo is simpler while there are two).
- Thor install path: plain Arch Linux ARM plus the device packages from another project's repo (checked
  2026-10-13; ROCKNIX itself is not Arch and has no pacman packages):
  - **Pocknix** (github.com/shuuri-labs/pocknix-os, GPL-2.0, ~260 stars, active): its own pacman repo (a
    snapshot of ALARM plus its packages; "rolling mode" points the rest at ALARM). Device packages:
    `linux-pocknix-sm8550` (7.2.10, ROCKNIX's patch stack + Pocknix's suspend/UFS/PCIe fixes, all board
    dtbs), `pocknix-firmware-sm8550`, `pocknix-bootloader-sm8550`, `pocknix-bsp-sm8550` (InputPlumber maps
    incl. the Thor's), a Mesa build, FEX, gamescope. PKGBUILDs public; the kernel package is "thin" (it
    packages their `make kernel` output).
  - **Thorch** (github.com/thorch-os/thorch): `linux-thorch`, `thorch-firmware-rocknix`, `thorch-mesa`,
    `thorch-fex-bin`, InputPlumber packages; SD image builder.
  Plan: ALARM `core`/`extra`/`alarm` + Pocknix's repo for the kernel, firmware, bootloader and BSP only (check
  their dependencies and package signing first); our layers on top. Fallback: build their PKGBUILDs
  ourselves into our own repo.
- When to archive this repo: after the README says so and points to the new one.

**Steps:**
1. Pick names; create the new repo (private until it has a first working state).
2. Bring over the layer setup and base configs; set up the desktop for development (nested and headless sway,
   two outputs).
3. The system program: `core` and `library` first (unit tests), then `policy`, then `ui`.
4. The Thor arrives. Recovery first. EDL on the Thor (github.com/nathanwongg/ayn-thor-flash-guide-on-macOS,
   tested on a Thor): power off, hold Volume Down (or Vol+ and Vol-) and plug USB into the computer; the
   screen stays black (EDL, USB 05c6:9008). That happens before ABL runs, so it works with a broken ABL.
   Flash with Linaro's open `qdl` (no Windows/QFIL): `qdl --storage ufs --include . xbl_s_devprg_ns.melf
   rawprogram*.xml patch*.xml` (stop ModemManager first) and AYN's Thor firmware zip (programmer
   `xbl_s_devprg_ns.melf`, signed by AYN). Not public (searched 2026-10-14): AYN's software page
   (raw HTML checked) has only Loki drivers and Odin 1 packages; github.com/AYNTechnologies has only kernel,
   u-boot, ALSA/AudioReach sources; XDA has a Thor OTA zip (`Thor_20251120_ota.zip`), which has no EDL
   programmer. Ask AYN support for the Thor's EDL (QFIL) package. Get
   the zip onto the desktop and build `qdl` before flashing ROCKNIX ABL; root Android and
   dump every partition (`/dev/block/by-name/*`, both A/B slots) to the desktop; ROCKNIX ABL's
   install (checked 2026-10-14, `ROCKNIX/distribution` `packages/tools/rocknix-abl`: `backup_abl.sh`,
   `flash_abl.sh`, `restore_backup_abl.sh`) `dd`s the same image to **both** `abl_a` and `abl_b`, with no checksum
   check in the Android script, so there is no fallback slot. A slot can't be switched for ABL alone (A/B
   switches every partition), so keep it simple and careful: check the image's sha256 against the release
   (v1.2 `abl_signed-SM8550.elf`, 258048 bytes, `8c27a709…7e398c`), back up both slots and copy the backups
   off the device, flash, read both partitions back and compare before rebooting. Batocera's wiki says the
   Thor's stock ABL has no Linux boot mode, so the flash is needed; make a rescue SD card with a working Linux (ABL can boot it past a broken
   internal install; repair with btrfs snapshots or the previous kernel package). Then device layer, install,
   the device tests (hibernate, latency, power).
5. Archive this repo: README status, link to the new repo.
