# Desktop (sway)

All files in `dotfiles/sway/` (symlinked to `~/.config/sway`).

- `config`: stock `/etc/sway/config` plus: `$menu` = fuzzel, `Mod+Backspace` kill, `Mod+Shift+Backspace` force-kill (`force-kill.sh`: TERM, then KILL after 1 s),
  `output DSI-1 transform 90`, volume/brightness keys → `osd.sh`, brightness on `Mod+volume`
  keys (the device has no brightness keys), turbo toggles on `Mod4+F11/F12`, panel resync on `Mod4+F10`. Includes `theme`,
  `autostart`, `handheld`.
- `theme`: Catppuccin Mocha palette, `default_border pixel 1`, muted focus colours, no title
  bars, `gaps inner 4` with `smart_gaps`/`smart_borders` (lone windows have no border/gap),
  wallpaper `wallpaper.jpg` (1280x720, Lanczos resize in linear light) on `$crust`. Bar on top.
- Bar: `~/.local/bin/statusbar`, built from `src/statusbar.c` (one C file, event driven; the
  header comment explains every source). `cpu 2.4G gpu 400M | ram 0.6/3.7G | <ssid> N% |
  vol N% | bat N% | clock`. CPU = fastest core, GPU `idle` = ≥90% of the last second in RC6;
  both dimmed while turbo is capped. Reads are spaced by cost: CPU/GPU clock every second, RAM
  5 s, WiFi signal (/proc/net/wireless, asks the firmware, 2.3 ms) 30 s, battery (I2C, 9 ms)
  60 s and at once on charger uevents, SSID (iwctl) on link changes, volume (wpctl) and turbo
  state when `osd.sh`/`turbo.sh` send SIGUSR1. The screen-off idle step SIGSTOPs it, resume
  SIGCONTs it. Only changed lines are written. Measured: sway+swaybar+statusbar together
  1.7 ms CPU/s (0.17% of a core); the GPU stays in RC6.
- `autostart`: Pegasus on workspace 1 (`assign` + `exec ~/.config/pegasus-frontend/run`),
  `~/.local/bin/inputd`, `for_window [app_id="pegasus-tool"] fullscreen enable`.
- `handheld`: `seat * hide_cursor 3000`, swayidle (power.md), wob pipeline
  (`$XDG_RUNTIME_DIR/wob.sock`).
- `osd.sh vol-up|vol-down|mute|bri-up|bri-down`: changes the value (wpctl / brightnessctl with
  `--min-value=2`) and writes it to wob. Idle dimming deliberately doesn't show wob.
- `inputd` (`src/inputd.c`): the pad's Guide button (BTN_MODE). Starts Pegasus if not running,
  focuses workspace 1 if it is, does nothing while a game launched by Pegasus (a child process)
  runs. The kernel only delivers BTN_MODE to it (EVIOCSMASK), and inotify on /dev/input reopens
  the pad when it reappears (screen off, resume), so it never polls.
- C programs (`src/`): manifest `build` entries; `scripts/sync install` compiles them when the
  source is newer (`sync check` reports `BUILD outdated`). Restart after a rebuild:
  `swaymsg reload` (statusbar, swaybar's child) and
  `pkill -x inputd; swaymsg exec ~/.local/bin/inputd`.
- `turbo.sh`: see power.md. Sends SIGUSR1 to statusbar so the dimming updates at once.

Restarting wob: `pkill -x wob; pkill -x tail`, then `swaymsg exec` the pipeline from `handheld`.
`swaymsg reload` does not re-run `exec` lines.

## Other programs
- foot (`dotfiles/foot/foot.ini`): DejaVu Sans Mono 10, black background `alpha=0.9`, Catppuccin
  palette, 10k scrollback, non-blinking beam cursor. Section is `[colors-dark]` (foot ≥1.24).
- fuzzel: themed to match, rounded border.
- wob (`dotfiles/wob/wob.ini`): bottom bar, theme colours; `[style.cpu]`/`[style.gpu]` for turbo.
- helix is `EDITOR`/`VISUAL` (fish universal variables; binary is `helix`, not `hx`).
- fish: functions `reboot`, `poweroff`, `suspend`, `hibernate` wrap `sudo systemctl ...`
  (passwordless rule). `config.fish` adds `~/.local/bin` to PATH (Claude Code).

## Input
- RetroArch keyboard input uses the `wayland` driver. The pad is read via udev by games.

## Display
- Panel mode 720x1280 @ 60.253 Hz, rotated (`transform 90`). RetroArch's `video_refresh_rate` is set to
  60.253 so its audio/video sync doesn't bend the pitch to fit 60.000.
- `subpixel none`: grey text antialiasing everywhere (8-bit glyph caches). The kernel reports the
  panel's native `rgb`, which is wrong after rotation (it would be `vbgr`); apps don't correct for it.
- No VRR (DSI panel), no tearing/direct scanout: the display engine can't rotate a plane by 90
  degrees, so sway composites (rotates) every frame, including fullscreen games.
- `max_render_time 12` (sway composites 12 ms before each vblank). Measured by tracing
  `i915:intel_pipe_update_end` and counting jumps in its hardware `frame=` counter (refreshes
  without a new image), 20 s per run:

  | Load | off (default) | 10 ms | 12 ms | 14 ms |
  |---|---|---|---|---|
  | RetroArch snes9x F-Zero | 13.0-13.6% | 3.3% | 0% | - |
  | same + crt-royale shader | 11.0-13.4% | 5.5% | 0% | 0% |
  | Hyper Light Drifter (Wine, Xwayland) | 12.4-13.8% | - | 0.3% | 0.4% |

  4 and 6 ms were as bad as off. If a game stutters, retest with the same method before changing it.
