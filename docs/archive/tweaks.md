# GPD Win 1 – system tweaks log

Dedicated Arch Linux gaming handheld. Last updated 2026-10-06.
Baseline boot (before any tweak, measured with `systemd-analyze`): **24.393s** =
firmware 7.1s + loader 2.0s + kernel 2.6s + initrd 5.0s + userspace 7.7s.

Measurements:
| Boot | Firmware | Loader | Kernel | Initrd | Userspace | Total | getty@tty1 starts at |
|---|---|---|---|---|---|---|---|
| Baseline | 7.1s | 2.0s | 2.6s | 5.0s | 7.7s | 24.4s | 7.68s (userspace) |
| 2026-10-06 20:36, after user-sessions override | 7.9s | 1.9s | 2.5s | 3.5s | 6.1s | 21.9s | 4.94s (userspace) |

The login-path gain (~2.7s) is from the override; `iwd` is no longer in getty's critical chain. The 1.5s initrd drop is likely noise (nothing changed there), and firmware varies by ~0.8s between boots. Sway started 13s after kernel start (was 16s).

## Active changes

### Boot / systemd
| What | Where | Why | Revert |
|---|---|---|---|
| Logins no longer wait for `network.target` (iwd ~1.4s) | full override `/etc/systemd/system/systemd-user-sessions.service` (stock unit minus `network.target` in `After=`) | getty/sway can start ~1.4s earlier; Wi-Fi comes up in the background | `sudo rm /etc/systemd/system/systemd-user-sessions.service && sudo systemctl daemon-reload` |

Notes:
- Must be a full override. An empty `After=` in a drop-in does not remove ordering dependencies (tested).
- It is a frozen copy of the stock unit; recheck it after systemd updates.
- Verify after reboot: `systemd-analyze critical-chain getty@tty1.service` should no longer list `iwd`.

### CPU / GPU turbo toggles
- `Mod4+F11` toggles CPU turbo (`intel_pstate/no_turbo`), `Mod4+F12` toggles GPU turbo (i915 max+boost freq 600 MHz <-> RP1 400 MHz). Bound with `--locked` in `~/.config/sway/config` (backup: `config.orig-turbo`).
- Script: `~/.config/sway/turbo.sh cpu|gpu`. It reads the state back from sysfs and shows it in wob: full bar = on, empty = off; purple = CPU, green = GPU (styles `[style.cpu]`/`[style.gpu]` in `~/.config/wob/wob.ini`, backup: `wob.ini.orig`).
- `/etc/tmpfiles.d/turbo.conf` makes `no_turbo`, `rps_max_freq_mhz`, `rps_boost_freq_mhz` writable by group `wheel` at each boot (no extra sudo rules). Revert: delete it and reboot (or `chmod 644` the three files).
- Settings reset to turbo-on at boot. CPU is an Atom x7-Z8700 (1.6 GHz base).
- First version had a bug (computed the new state without reading it back, so a failed write always showed an empty bar). Fixed.
- Restarting wob: do not use `pkill -f` with a pattern that appears in your own command line, it kills the shell. Use `pkill -x wob` and `pkill -x tail`, then `swaymsg exec` the same pipeline as `~/.config/sway/handheld`.

### Memory / swap
- **zswap**: already on by default in the Arch kernel (`CONFIG_ZSWAP_DEFAULT_ON=y`, zstd, 20% pool). Nothing configured.
- Swap is the 4 GB eMMC partition (`/dev/mmcblk0p2`), also the hibernation target (hibernation already worked and was left untouched).
- I tried zram first, then removed it. No zram config, package or sysctl file remains.
- Runtime `vm.swappiness=60`, `vm.page-cluster=3` were set and match defaults. No persistent sysctl files.

### Shell / editor
- `EDITOR` and `VISUAL` = `helix`, as fish universal variables (`set -Ux`). Binary is `helix`, not `hx`.

### sudo
- `/etc/sudoers.d/21-firmware-setup`: `robin ALL=(root) NOPASSWD: /usr/bin/systemctl reboot --firmware-setup` (exact command only). Used by the Pegasus BIOS entry.
- Pre-existing, not mine: `10-global-timestamp` (`timestamp_type=global`) and `20-power` (NOPASSWD reboot/poweroff/suspend/hibernate/suspend-then-hibernate).

### Packages installed
- `zelda3-git` (AUR, r318, built from upstream snesrev/zelda3 with a US v1.0 ROM, sha256 checked).
- Dependencies pulled in: `python-pillow`, `python-yaml`, `clang`, `sdl2`. `clang` is build-only and large (~100 MB+); remove with `sudo pacman -Rns clang` if you do not need it.
- `zram-generator` was installed then removed.

### Zelda 3 (`/opt/zelda3-git/zelda3.ini`, original at `zelda3.ini.orig`)
| Option | Was | Now |
|---|---|---|
| `ExtendedAspectRatio` | `4:3` | `extend_y, 16:9` (426x240, exact 3x on the 1280x720 panel) |
| `Fullscreen` | 0 | 1 |
| `Autosave` | 0 | 1 |
| `ItemSwitchLR` | 0 | 1 |
| `TurnWhileDashing` | 0 | 1 |
| `SkipIntroOnKeypress` | 0 | 1 |
| `MiscBugFixes` | 0 | 1 |
| `CancelBirdTravel` | 0 | 1 |

Saves live in `/opt/zelda3-git/saves` (world-writable). Uninstalling the package deletes that directory, so back saves up first.
Gamepad ("Microsoft X-Box 360 pad") mapping left at the default; not tested in-game yet.

### Pegasus (Games frontend)
- `~/Games/pc/`: added `zelda3.sh`, `media/zelda3.png` (libretro box art on 512x512), and a metadata entry.
- `~/Games/utils/`: added `tools/bios.sh`, `media/bios.png` (generated chip icon matching the other tools), and a "BIOS Setup" metadata entry. It asks for Enter, then runs `sudo systemctl reboot --firmware-setup`.
- Restart Pegasus to pick up both entries.
- Fix: `tools/bios.sh`, `update.sh`, `pegasus-update.sh` and `boxart.sh` used `read -P "..." _`. This fish version rejects `_` ("cannot overwrite read-only variable"), so the prompt failed at once and the terminal closed (BIOS tool flashed and returned; the others closed right after finishing). Changed `_` to `ans` in all four. Syntax-checked with `fish -n`; not yet run end to end from Pegasus.

## Base install and setup (installer session, 2026-10-06)

Everything from the reinstall up to the Utilities collection. Done over ssh from the PC.

### Install
- eMMC repartitioned (GPT): p1 512 MiB ESP at `/boot` (FAT32), p2 4 GiB swap, p3 ext4 `/`. fstab and the boot entry use UUIDs (the eMMC shows up as `mmcblk0` or `mmcblk2` depending on boot).
- Bootloader: systemd-boot, `timeout 0`, kernel options `fbcon=rotate:1`. `systemd-boot-update.service` enabled. Stale GRUB EFI entry removed.
- Firmware: only `linux-firmware-broadcom` (Wi-Fi BCM4356 + GPD-specific nvram) and `linux-firmware-intel` (audio uses the legacy `intel_sst_acpi` driver with `fw_sst_22a8.bin`, not SOF). Bluetooth patch `BCM4356A2.hcd` is missing (not in the repos; USB variants from winterheart/broadcom-bt-firmware are untested).
- Network: iwd with built-in DHCP (`/etc/iwd/main.conf`) + systemd-resolved; `wireless-regdb` installed and `/etc/modprobe.d/cfg80211.conf` sets `ieee80211_regdom=NO`.
- User `robin` (groups wheel, video, input, seat), shell fish. tty1 autologin (`getty@tty1` drop-in with `-o '-p -f -- \\u'`), fish `conf.d/sway.fish` runs `exec sway` on tty1. Sway uses seatd.

### Hardware quirks fixed
| Problem | Fix |
|---|---|
| Backlight: `Failed to get the SoC PWM chip` | `MODULES=(pwm-lpss-platform i915)` in `/etc/mkinitcpio.conf` |
| Portrait panel | `fbcon=rotate:1`; sway `output DSI-1 transform 90` |
| Suspend aborted (xHCI `EBUSY -16`) | `/etc/udev/rules.d/90-xhci-no-wakeup.rules` disables wakeup on the USB3 controller (pad/keyboard can no longer wake it; power button and lid can) |
| Brightness drops to minimum after hibernate | `/usr/lib/systemd/system-sleep/backlight` saves/restores it |
| Old install froze after resume | Not seen since the xHCI fix (5/5 s2idle cycles, lid suspend-then-hibernate and plain hibernate all resumed) |

Only s2idle exists (no S3). The RTC alarm does not wake it from hibernation; the power button does. Hibernate resume finds the swap via the systemd EFI variable, no `resume=` needed.

### Sleep / power
- `/etc/systemd/sleep.conf.d/handheld.conf`: `MemorySleepMode=s2idle`, `HibernateDelaySec=30min`.
- `/etc/systemd/logind.conf.d/handheld.conf`: lid and power key → `suspend-then-hibernate`.
- `low-battery.timer` (every 2 min) runs `/usr/local/bin/low-battery-hibernate`: hibernates at ≤5% while discharging.
- swayidle (`~/.config/sway/handheld`): dim to a third after 2 min (`dim.sh`), screen off after 3 min, suspend after 10 min on battery only (`idle-suspend.sh`). Gamepad input does not count as activity.
- Bluetooth `rfkill block`ed (persisted by systemd-rfkill). Wi-Fi power save was already on.
- Timers: `fstrim.timer`, `paccache.timer` (`PACCACHE_ARGS=-k2` in `/etc/conf.d/pacman-contrib`).

### sudo / ssh
- `/etc/sudoers.d/10-wheel` (`%wheel ALL=(ALL:ALL) ALL`) and `20-power` (NOPASSWD systemctl reboot/poweroff/suspend/hibernate/suspend-then-hibernate). Order matters: the NOPASSWD file must sort after `10-wheel` (last match wins).
- fish functions `reboot`, `poweroff`, `suspend`, `hibernate` wrap `sudo systemctl …`.
- sshd is key-only (`/etc/ssh/sshd_config.d/10-keys-only.conf`); the PC's ed25519 key is in `~/.ssh/authorized_keys`.
- Gotcha: three failed sudo passwords trigger pam_faillock (10 min lockout). Never pipe the password into `sudo -S` while stdin is also a heredoc script.

### Sway / desktop
- Packages: sway, swaybg, swayidle, foot, fuzzel, wob, brightnessctl, grim, ttf-dejavu, btop.
- `~/.config/sway/config` = stock config + includes `theme`, `autostart`, `handheld` (original kept as `config.orig`). `$menu` is fuzzel; Mod+Backspace closes a window; brightness on Mod+volume keys.
- `theme`: Catppuccin Mocha colours, 1 px subtle borders, no title bars, `smart_borders`/`smart_gaps`, wallpaper `wallpaper.jpg` (1280x720, gamma-correct Lanczos resize).
- Bar: `status.sh` (i3bar JSON) shows Wi-Fi SSID and signal, battery (yellow ≤30%, red ≤15%, `+` when charging) and clock, every 20 s.
- `osd.sh` drives wob for volume/brightness keys (volume via wpctl). Cursor hides after 3 s.
- foot: black background with alpha 0.9, Catppuccin palette, 10k scrollback, beam cursor. fuzzel themed to match.
- `guide-button.py` (started from `autostart`): Guide button starts Pegasus, or switches to workspace 1; does nothing while a game launched from Pegasus is running.

### Audio
- PipeWire + WirePlumber + `alsa-ucm-conf` (speaker sink `cht-bsw-rt5645`), 48 kHz.
- Stutter in SDL games came from 22050 Hz output being resampled; DevilutionX set to `Sample Rate=48000`.

### Pegasus
- `pegasus-frontend-stable-git` built with makepkg: yay cannot build it (the PKGBUILD echoes at top level, which breaks `--packagelist`). Update with the fish function `pegasus-update`. Needs `qt5-wayland` and `sdl2-compat`. makepkg debug packages disabled in `~/.config/pacman/makepkg.conf`.
- Started via `~/.config/pegasus-frontend/run`, which puts a `dbus-send` shim first in PATH: Pegasus' Suspend/Reboot/Power off go to the NOPASSWD sudo rules (Suspend = suspend-then-hibernate) instead of logind, which needs polkit.
- Collections (each dir listed in `~/.config/pegasus-frontend/game_dirs.txt`): `~/Games/pc` (shortname `pc` gives the IBM logo; games need a `file:`, so each has a small launcher script), `~/Games/snes`, `~/Games/utils`.
- Do not set `assets.logo` on games: the grid theme then shows the logo instead of the title.
- `~/Games/fetch-boxart.py`: fetches libretro box art for ROM folders (title match, prefers USA) into `media/<rom>/boxFront.png` and writes clean titles into a generated block of each `metadata.pegasus.txt`.
- Utilities tools: Terminal, System Monitor, Update System, Update Pegasus, Fetch Box Art, WiFi, RetroArch (+ BIOS Setup from the other session). Terminal tools use `foot --app-id=pegasus-tool`, made fullscreen by a sway rule.

### RetroArch (`~/.config/retroarch/retroarch.cfg`)
- Cores: nestopia, snes9x, gambatte, mgba, genesis-plus-gx, picodrive, beetle-pce-fast, beetle-psx (needs a BIOS in `~/.config/retroarch/system/`). Pad profile for "Microsoft X-Box 360 pad" in `autoconfig/udev/` (Guide→menu line commented out there).
- `config_save_on_exit = false`: save settings from the menu explicitly. Test runs should pass `--appendconfig` with `config_save_on_exit = "false"`; `--max-frames` disables throttling, so it is useless for pacing tests.
- Pacing (was ~25% dropped frames): gl, vsync on, `video_max_swapchain_images = 2`, `video_fullscreen_x/y = 1280/720` (it detects the panel as 720x1280), audio `pipewire`, `audio_latency = 128`, `audio_rate_control_delta = 0.020`, `audio_out_rate = 48000`. Result: 0 dropped frames, audio still reports some underruns.
- `input_driver = wayland`, auto save/load state on, shader dir `~/.config/retroarch/shaders` (Arch's default path does not exist).
- Hotkeys: Guide = hotkey enable; Guide+L2 menu, Guide+L/R load/save state, Guide+R2 hold fast-forward, Esc quits.

### Other packages
- base-devel, git, yay-bin (`yay-bin-debug` also got installed and can be removed), pegasus, retroarch + xmb/ozone assets + core-info, devilutionx-bin (Diablo/Hellfire data in `~/.local/share/diasurgical/devilution/`), iw, pacman-contrib.
- `gog_psychonauts_2.0.0.4.sh` (native Linux, 32-bit) is in `~` but not installed; it needs multilib.

## Ideas not done yet (ranked)
1. Add `quiet` to the options in `/boot/loader/entries/arch.conf` (console is rotated with `fbcon=rotate:1`, printing boot messages is slow). Back up the entry first.
2. Initrd trim: `COMPRESSION="lz4"` and/or dropping `sd-vconsole` from `HOOKS` in `/etc/mkinitcpio.conf` (about 0.2s each, rebuild with `mkinitcpio -P`). The initrd also carries ~51 useless i915 firmware files.
3. BIOS (AMI Aptio 5.11, 2016; Delete key, or Pegasus "BIOS Setup"): look for boot logo, Fast Boot, setup prompt timeout, network/PXE stack, boot order. Firmware is 7s of the 24s. Change one at a time.
4. Disabling `sshd` does not speed up boot (it is not on the login path); it only saves a little RAM.
5. Optional: remove `clang`, disable unused `systemd-userdbd.socket`.

## Findings worth remembering
- Where boot time goes: initrd is CPU-bound module loading (udev coldplug ~3s CPU on the Atom), then `initrd-switch-root` 2.3s cold-reading the real root from eMMC, loader 2s reading the 17 MB kernel + 21 MB initrd.
- loader.conf already has `timeout 0`; `/boot/intel-ucode.img` (15 MB) is not used by the entry (microcode is embedded in the initramfs).
- The Win 1 gamepad has modes: in "Mouce for Android" mode (USB 0079:1a00) it shows up as mouse/keyboard; only in gamepad mode does it appear as `Microsoft X-Box 360 pad`. `~/.config/sway/guide-button.py` looks for that name.
- fish gotcha: never use `_` as the variable in `read` (read-only in this fish). Use a named variable.
- RAM at idle: ~760 MB used; Claude Code is about half of it.

## Leftovers in the session scratchpad (safe to delete)
`aur/zelda3-bin` (built, never installed), `aur/zelda3-git` build dir with a copy of the ROM, and `art/`. All under `/tmp/claude-1000/.../scratchpad/`.
