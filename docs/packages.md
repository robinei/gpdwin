# Packages

Minimal by intent: install only what is used, prefer official repos. `pacman -Qqe` is the
truth; this explains the choices.

- Base: `base linux linux-firmware-broadcom linux-firmware-intel intel-ucode` (only the two
  firmware sub-packages this hardware needs), `iwd`, `openssh`, `sudo`, `fish`, `vim`, `helix`,
  `wireless-regdb`, `iw`, `pacman-contrib` (checkupdates, pacdiff, paccache).
- Desktop: `sway swaybg swayidle foot fuzzel wob brightnessctl grim ttf-dejavu btop`, seatd
  (sway dependency, `seatd.service` enabled, user in group `seat`).
- Audio: `pipewire pipewire-pulse pipewire-alsa wireplumber alsa-ucm-conf` + `realtime-privileges`
  (robin in group `realtime`, so PipeWire gets RT scheduling without RTKit/polkit) (all explicit;
  pulse and ALSA apps both need their PipeWire bridge).
- Frontend: `retroarch`, assets, core-info, cores (frontend.md), `qt5-*` for Pegasus.
- Windows games: `wine` (repo, WoW64 build: no multilib needed) + `ntsync-autoload`. No DXVK.
  `xorg-xwayland` (+ small deps, 4.4 MiB) so Wine uses X11 (its Wayland driver mishandles the rotated panel).
- Build: `base-devel git` (AUR builds). `python` is explicit (guide-button.py, scripts).
  `clang` came with zelda3-git and is build-only (~100 MB+).
- Installed by the user or other work: `firefox`, `yazi`, `7zip`.
- Network: hostname `gpdwin.lan` via the router's DNS (iwd sends the hostname: `[IPv4]
  SendHostname=true` in `/var/lib/iwd/VennensVenner.psk`, not in the repo because it holds the
  Wi-Fi key; re-add it if the network is re-created). systemd-resolved with LLMNR and mDNS off
  (`resolved.conf.d/90-gpd.conf`). Firewall: `/etc/nftables.conf` (nftables.service), incoming
  dropped except ssh from 192.168.1.0/24 and the LAN's IPv6 prefixes, DHCP replies, ICMP.
  The desktop is `desktop.lan` (192.168.1.216).
- iwd does DHCP itself (`/etc/iwd/main.conf`: `EnableNetworkConfiguration=true`,
  `NameResolvingService=systemd`), DNS via systemd-resolved, time via systemd-timesyncd.
- Timers: `fstrim.timer` (eMMC supports discard), `paccache.timer` (`-k2`), `low-battery.timer`.

## AUR policy
- Vendored: `aur/pkgbuilds/PKG/` holds each package's build files exactly as reviewed (exported
  from the reviewed AUR commit, `.aur-commit`). `scripts/aur build/mark` refresh it, so updates
  show as git diffs; `scripts/aur build PKG --vendored` builds without the AUR.
- Our changes: `aur/patches/PKG/pkgbuild/*.patch` (build files; .SRCINFO regenerated) and
  `aur/patches/PKG/*.patch` (extracted source, dir from `aur/patches/PKG/target`). Builds with
  patches always start from pristine sources (src/ is removed; note `git clean -fdx` does NOT
  remove nested git repos, `-ffdx` does). Patched packages need no fixes after installing.
- Files that can't be in the repo (game ROMs) go in `~/.local/share/gpd/aur-local/PKG/` and are
  copied into the build dir (zelda3-git: `zelda3.sfc`, US v1.0, sha256 66871d66...).
- `-git` packages are pinned to a commit with a build-file patch where the PKGBUILD doesn't
  (zelda3-git: snesrev/zelda3 45a149d, also `backup=` for zelda3.ini so rebuilds keep our config).
- `scripts/aur build` checks sudo first and keeps the timestamp alive during long builds.
- Current patches: pegasus-frontend-stable-git (adds qt5-wayland/sdl2-compat deps, tolerant
  .install, no top-level echo; splash progress animation and grid-theme spinner only run while
  visible), zelda3-git (pin + backup).
- AUR packages are built only via `scripts/aur` after reviewing the diff since the last
  reviewed AUR commit (`aur/reviewed.tsv`). `scripts/update` and `/maintain` do this.
- Current AUR set: `pegasus-frontend-stable-git`, `devilutionx-bin`, `zelda3-git`, `steamdepotdownloader-bin` (yay-bin removed 2026-10-07).
  `-git` packages build upstream HEAD, which the PKGBUILD review doesn't cover.
- No AUR helper: `scripts/aur search` (AUR RPC) replaces `yay -Ss`; builds only via `scripts/aur`.
- `~/.config/pacman/makepkg.conf`: `MAKEFLAGS="-j$(nproc)"`, `OPTIONS+=(!debug)`.
