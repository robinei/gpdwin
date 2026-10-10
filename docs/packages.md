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
- Frontend: shelf (`src/shelf.c`): `sdl3_image`, `sdl3_ttf` (with `sdl3`, `sqlite` and `icu`, already
  installed). `retroarch`, assets, core-info, cores (frontend.md). `qt5-*` only for Pegasus, kept
  installed without integration (frontend.md).
- PSP: standalone `ppsspp` (SDL build, `PPSSPPSDL`; pulls `ppsspp-assets`, `openxr`, `libzip`,
  `miniupnpc`), installed 2026-10-09.
- Windows games: `wine` (repo, WoW64 build: no multilib needed) + `ntsync-autoload`. No DXVK.
- Multilib (enabled 2026-10-07, `system/etc/pacman.conf`) for 32-bit-only native Linux ports
  (e.g. Psychonauts): lib32 glibc/gcc-libs (now in core), mesa, libglvnd, vulkan loader + intel,
  pipewire/libpulse/alsa-lib/alsa-plugins (32-bit games play through PipeWire), X11 libs,
  freetype2/fontconfig, png/jpeg/zlib/vorbis/ogg, curl. About 300 MB. 32-bit SDL 1.2/2 and
  OpenAL are AUR-only now (lib32-sdl12-compat, lib32-sdl2-compat, lib32-openal); most old ports
  bundle their own, so add them via /aur-add only when a game needs them. Installed:
  `lib32-openal` (SteamWorld Heist needs libopenal.so.1), built without JACK/PortAudio. The game installer
  then offers 32-bit Linux builds and drops `-osarch 64`. To undo: remove the lib32 packages,
  comment out [multilib].
  `xorg-xwayland` (+ small deps, 4.4 MiB) so Wine uses X11 (its Wayland driver mishandles the rotated panel).
- Display debugging: `intel-gpu-tools` (`intel_reg read 0x18b804` etc., needs sudo; installed 2026-10-09).
- DKMS (`dkms`, `linux-headers`): builds our two patched audio modules (`kernel/`, docs/hardware.md "Audio") for each kernel; keep both installed. Debugging tools (2026-10-09, removable): `linux-headers` (needed to build `kernel/rt5645`), `alsa-utils`, `acpica` (iasl), `intel-gpu-tools`; remove what isn't needed. `i2c-tools` is used by the `pmic-charger` sleep hook (power.md).
- Profiling: `perf` (installed 2026-10-09 to find a Wine bottleneck; run as `sudo perf record -g -p PID`).
- Performance overlay: `mangohud-light` and `lib32-mangohud-light` (for 32-bit games), one own
  rebuild of the repo package without mangoplot/mangoapp and their ~134 MB of
  python/matplotlib/numpy/glfw, building both halves from one source so our source patches
  (battery detection) cover both; replaces multilib's `lib32-mangohud`. See frontend.md.
- Native game libs: `sdl2_net` (Strife: Veteran Edition, 2026-10-08).
- Build: `base-devel git` (AUR builds). `python` is explicit (scripts). `gcc` (base-devel) also builds `src/*.c`.
  `clang` came with zelda3-git and is build-only (~100 MB+).
- Installed by the user or other work: `firefox`, `yazi`, `7zip`.
- Network: hostname `gpdwin.lan` via the router's DNS (iwd sends the hostname: `[IPv4]
  SendHostname=true` in `/var/lib/iwd/VennensVenner.psk`, not in the repo because it holds the
  Wi-Fi key; re-add it if the network is re-created). systemd-resolved with LLMNR and mDNS off
  (`resolved.conf.d/90-gpd.conf`). Firewall: `/etc/nftables.conf` (nftables.service), incoming
  dropped except ssh from 192.168.1.0/24 and the LAN's IPv6 prefixes, DHCP replies, ICMP.
  `nftables.service` is a oneshot that exits after loading, so `systemctl is-active` says
  inactive: check with `sudo nft list ruleset` instead.
  The desktop is `desktop.lan` (192.168.1.216).
- iwd does DHCP itself (`/etc/iwd/main.conf`: `EnableNetworkConfiguration=true`,
  `NameResolvingService=systemd`), DNS via systemd-resolved, time via systemd-timesyncd.
- Wi-Fi not switching networks (2026-10-10, open): after leaving home, iwd sat in `autoconnect_full`
  for 17 min without finding the known iPhone hotspot; a reboot connected at once. Either the hotspot
  wasn't visible to background scans (iPhones often only beacon while the hotspot screen is open) or
  the brcmfmac firmware stopped returning scan results. Utilities "WiFi"
  (`pegasus/utils/tools/wifi-menu`) shows what a fresh scan sees and has "reset Wi-Fi":
  `/usr/local/bin/wifi-reset` (sudoers `23-wifi-reset`) logs what `iw` scan sees, then stops iwd,
  reloads brcmfmac and starts iwd. Its log (`journalctl -t wifi-reset`) tells the two causes apart.
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
- Current patches: zelda3-git (pin + backup), commander-genius-git (pin to v3.6.3 commit bee4fcb, `--parallel 2`
  so the C++ build fits in 3.7 GB RAM; ~35 min build).
- AUR packages are built only via `scripts/aur` after reviewing the diff since the last
  reviewed AUR commit (`aur/reviewed.tsv`). `scripts/update` and `/maintain` do this.
- Current AUR set: `devilutionx-bin`, `zelda3-git`, `steamdepotdownloader-bin`, `lib32-openal`, `ludusavi-bin`, `commander-genius-git`
  (multilib, patched: no JACK/PortAudio), `lib32-sdl2-compat`, `lib32-sdl12-compat`, plus our rebuild `mangohud-light` of the repo package
  (aur/pkgbuilds/mangohud-light, `.arch-package`). yay-bin removed 2026-10-07.
  `-git` packages build upstream HEAD, which the PKGBUILD review doesn't cover.
- No AUR helper: `scripts/aur search` (AUR RPC) replaces `yay -Ss`; builds only via `scripts/aur`.
- `~/.config/pacman/makepkg.conf`: `MAKEFLAGS="-j$(nproc)"`, `OPTIONS+=(!debug)`.
