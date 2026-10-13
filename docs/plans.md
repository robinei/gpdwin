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
- Programs that work on their own stay small and separate if they fit no better inside (statusbar, scripts).

**Open questions:**
- **Building on the device** (less pressing now: the GPD unit is probably retired, 2026-10-13, and the Thor
  and the Ally build Rust fine). `scripts/sync` compiles the C programs on the device in seconds. A Rust release
  build on the GPD's Atom (4 GB) would take a long time and may run out of memory. Options: build on the
  device anyway where it is fast (the Thor), cross-compile on a desktop and copy the binary (sync kind
  `cargo`, building where `cargo` is fast), or prebuilt binaries per architecture (x86_64, aarch64) from a
  release. Decide before porting.
- Drawing: CPU or GPU. shelf uses SDL3's renderer now; SDL3 also works from Rust (`sdl3` crate) and would keep
  the current look and timing, at the cost of mixing SDL's window with our own layer-shell surfaces.
- Its own repository: the program is useful without this repo's configs (any sway handheld). It could live in
  its own repo, packaged for Arch (PKGBUILD), with this repo installing it like any other package.
- Order: build the new program against the new device (the Thor) first, where the second screen and the pad
  layer are needed. The C programs stay for devices/gpd-win1 (other GPD owners) until the new one covers them.

## One system program

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
- statusbar can stay separate (swaybar runs it), or become a source of the same state for the sysinfo mode.

**Open questions:**
- Migration: grow shelf into it (it has the window and the state), then move inputd's policy in as a thread;
  keep inputd buildable until the merged program has run for a while.
- Whether SDL3 can give the layer-shell surfaces, or those use plain Wayland next to SDL's window (shelf
  already uses Wayland protocols directly, `src/vendor/`).
- Does a sway or GPU reset (DSI resync, Super+F10) disturb the surfaces; the policy thread must not care.

## Pad system layer (inputd)

**Goal:** control the system without a keyboard: workspaces, terminal, on-screen keyboard, windows, power. For
devices without a keyboard (ROG Ally, AYN Thor) and for the GPD with the pad in hand. Started as
`docs/ideas.md` item 15.1.

**Design so far:**
- One button is the **system button**, named per device in `device.conf` (new key, e.g. `layer_key=`, a key
  code, and the input device it comes from if not the pad). A tap does what the Menu key does now (shelf);
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
  | right stick, R1 / L1 | mouse pointer and clicks (uinput mouse) |
  | left stick | scroll |
  | L2 / R2 | brightness (or volume) down / up |
  | Start | power menu: suspend, hibernate, power off |
  | Select | screenshot |

- Feedback while the button is held: a small overlay listing the bindings (on a second screen if there is one,
  else a swaybar line or a layer-shell popup).
- Emulators: shoulder buttons could send the emulator's save/load state keys when an emulator has the focus
  (ideas.md 15.1).

**Open questions:**
- Which button per device. GPD Win 1: the Guide button sends nothing in the pad's mouse mode
  (devices/gpd-win1/docs/hardware.md); check gamepad mode. ROG Ally: one of its extra buttons (`hid-asus`).
  AYN Thor: Home or Back (check what the `rsinput` driver reports).
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
- `device.conf` names the outputs (new keys, e.g. `main_output=DSI-2`, `second_output=DSI-1`). Without
  `second_output` nothing here runs, and the override modes fall back to popups on the main screen.
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
  - off: the panel powered down (`output … power off`).
  An override lasts until it is dismissed, then follow mode returns.
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
