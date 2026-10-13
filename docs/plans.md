# Plans

Features being designed, before they are built. Each section: the goal, the design so far, open questions.
When one is built, its description moves to the topic doc (`docs/desktop.md`, `docs/frontend.md`, ...), the
change gets a `docs/changelog.md` entry, and the section here is removed. Unsorted ideas stay in
`docs/ideas.md`.

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
