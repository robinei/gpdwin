# Packages

Minimal by intent: install only what is used, prefer official repos. `pacman -Qqe` is the
truth; this explains the choices.

- Base: `base linux linux-firmware-broadcom linux-firmware-intel intel-ucode` (only the two
  firmware sub-packages this hardware needs), `iwd`, `openssh`, `sudo`, `fish`, `vim`, `helix`,
  `wireless-regdb`, `iw`, `pacman-contrib` (checkupdates, pacdiff, paccache).
- Desktop: `sway swaybg swayidle foot fuzzel wob brightnessctl grim ttf-dejavu btop`, seatd
  (sway dependency, `seatd.service` enabled, user in group `seat`).
- Audio: `pipewire pipewire-pulse wireplumber alsa-ucm-conf`.
- Frontend: `retroarch`, assets, core-info, cores (frontend.md), `qt5-*` for Pegasus.
- Windows games: `wine` (repo, WoW64 build: no multilib needed) + `ntsync-autoload`. No DXVK.
- Build: `base-devel git` (AUR builds). `python` is explicit (guide-button.py, scripts).
  `clang` came with zelda3-git and is build-only (~100 MB+).
- Installed by the user or other work: `firefox`, `yazi`, `7zip`.
- Network: iwd does DHCP itself (`/etc/iwd/main.conf`: `EnableNetworkConfiguration=true`,
  `NameResolvingService=systemd`), DNS via systemd-resolved, time via systemd-timesyncd.
- Timers: `fstrim.timer` (eMMC supports discard), `paccache.timer` (`-k2`), `low-battery.timer`.

## AUR policy
- AUR packages are built only via `scripts/aur` after reviewing the diff since the last
  reviewed AUR commit (`aur/reviewed.tsv`). `scripts/update` and `/maintain` do this.
- Current AUR set: `pegasus-frontend-stable-git`, `devilutionx-bin`, `zelda3-git`, `yay-bin`,
  `steamdepotdownloader-bin`.
  `-git` packages build upstream HEAD, which the PKGBUILD review doesn't cover.
- yay stays for searching (`yay -Ss`) and `-G`. It can't build pegasus-frontend-stable-git (the
  PKGBUILD echoes at top level, which breaks `makepkg --packagelist` parsing).
- `~/.config/pacman/makepkg.conf`: `MAKEFLAGS="-j$(nproc)"`, `OPTIONS+=(!debug)`.
- `yay-bin-debug` was installed before debug packages were disabled; safe to remove.
