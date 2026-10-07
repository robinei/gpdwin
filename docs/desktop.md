# Desktop (sway)

All files in `dotfiles/sway/` (symlinked to `~/.config/sway`).

- `config`: stock `/etc/sway/config` plus: `$menu` = fuzzel, `Mod+Backspace` kill, `Mod+Shift+Backspace` force-kill (`force-kill.sh`: TERM, then KILL after 1 s),
  `output DSI-1 transform 90`, volume/brightness keys → `osd.sh`, brightness on `Mod+volume`
  keys (the device has no brightness keys), turbo toggles on `Mod4+F11/F12`, panel resync on `Mod4+F10`. Includes `theme`,
  `autostart`, `handheld`.
- `theme`: Catppuccin Mocha palette, `default_border pixel 1`, muted focus colours, no title
  bars, `gaps inner 4` with `smart_gaps`/`smart_borders` (lone windows have no border/gap),
  wallpaper `wallpaper.jpg` (1280x720, Lanczos resize in linear light) on `$crust`. Bar on top.
- `status.sh`: i3bar JSON; Wi-Fi SSID + signal (from `iwctl`, -90..-30 dBm → 0..100%), volume
  (`vol N%`, dimmed `muted N%`; `osd.sh` sends SIGUSR1 so it updates at once), battery
  (colours, see power.md), clock. Updates every 20 s to keep wakeups low.
- `autostart`: Pegasus on workspace 1 (`assign` + `exec ~/.config/pegasus-frontend/run`),
  `guide-button.py`, `for_window [app_id="pegasus-tool"] fullscreen enable`.
- `handheld`: `seat * hide_cursor 3000`, swayidle (power.md), wob pipeline
  (`$XDG_RUNTIME_DIR/wob.sock`).
- `osd.sh vol-up|vol-down|mute|bri-up|bri-down`: changes the value (wpctl / brightnessctl with
  `--min-value=2`) and writes it to wob. Idle dimming deliberately doesn't show wob.
- `guide-button.py`: reads the pad's Guide button (BTN_MODE). Starts Pegasus if not running,
  focuses workspace 1 if it is, does nothing while a game launched by Pegasus (a child process)
  runs. Reopens the device after resume. Needs `python` (marked explicit).
- `turbo.sh`: see power.md.

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
