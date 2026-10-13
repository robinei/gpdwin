# Ideas not done yet (ranked)

1. Initrd trim: `COMPRESSION="lz4"` and/or drop `sd-vconsole` from `HOOKS` (~0.2s each,
   `mkinitcpio -P`). The initrd also carries ~51 useless i915 firmware files.
2. BIOS (Delete key or Utilities "BIOS Setup"): boot logo, Fast Boot, setup prompt timeout,
   network/PXE stack, boot order. Firmware is ~7s of the boot. Change one at a time.
3. Psychonauts native install (multilib, 32-bit libs, Xwayland). See frontend.md.
4. Gamepad activity doesn't reset sway idle (dims after 2 min while browsing the frontend with the
   pad). Options: `inhibit_idle focus` for the frontend, or have inputd inhibit while the pad
   is used.
5. Remove `clang` (build-only, zelda3-git); disable unused `systemd-userdbd.socket`.
6. Bluetooth firmware patch if Bluetooth is ever wanted.
7. GOG source for `scripts/games` (`sources/gog.py`, e.g. lgogdownloader from the AUR).
8. Gamepad navigation in `scripts/games` (it runs in foot, so keyboard only for now).

9. If DSI resume failures (`flip_done timed out`) ever appear: see hardware.md "Prior art"
    (kernel patch, `i915.disable_power_well=0`, diagnostics capture).

10. Measure s2idle battery drain (unplugged, known sleep time; method in power.md); then
    consider a shorter HibernateDelaySec or the BIOS C-state limit.

11. Device-only Mesa (after the upload fix is settled; `devices/gpd-win1/mesa/`): gallium `crocus` + `softpipe`, Vulkan
    `intel_hasvk`, no rusticl/OpenCL, video codecs, docs or other vendors' drivers. The stock `libgallium`
    (55 MB) links `libLLVM` (172 MB) into every GL process (sway, frontend, games) only for drivers this device
    never uses. Expected: faster app start, less memory, builds in minutes. Measure start time and memory
    before/after. Costs: softpipe instead of llvmpipe as fallback, a larger diff to Arch's PKGBUILD per update,
    check that nothing needs a dropped part (Xwayland, kms_swrast). Same for `lib32-mesa` (Wine).
12. Other optimizations for the one known target (Atom x7-Z8700, Silvermont/Airmont, Gen8 CHV graphics, no LLC),
    now that packages can be built for it: e.g. `-march=silvermont`/`-mtune` builds of hot packages (Mesa,
    emulator cores, SDL), Mesa/driconf defaults that suit a non-LLC GPU, trimming libraries loaded by every
    process. Find candidates by measuring first (perf, startup traces), not by guessing.
13. Vulkan 1.3 on hasvk: shipped in Mesa 1.11 (2026-10-12, `devices/gpd-win1/mesa/README.md`, `devices/gpd-win1/mesa/reports/R9-vulkan13.md`). Left
    open: a real (still non-conformant) descriptor indexing for non-sampler descriptors (~300-500 lines, R9 section
    7.3), only useful if something besides DXVK >= 2.7 needs it; DXVK 2.7.1 works with the opt-in claim but felt
    slower than 2.4.1 in Dark Souls, so the games stay on 2.4.1.
4b. Relocation overhead (same report): small. GL (crocus) well under 1 % of a frame; hasvk 18-25 % of its own
    submit CPU time but still small per frame. Not worth a softpin port for speed.

Not worth doing: disabling `sshd` (not on the boot path), zram (zswap is already on).
14. f2fs root with LZ4 compression, for faster game loading (discussed 2026-10-12, not done). Measured: eMMC
    cold reads 106-124 MB/s (random 4/16/128 KB: 9/32/73 MB/s); LZ4 on this Atom decompresses 1050-1600 MB/s,
    zstd-1 300-860 MB/s. Game samples: Psychonauts 1.00x (no gain), Bastion 1.23x (~+13% read speed with LZ4,
    ~+8% btrfs zstd), Caves of Qud 1.49x (~+29% LZ4, ~+4% zstd). So f2fs+LZ4 is the speed choice, btrfs (no LZ4)
    isn't. f2fs compression without `release_cblocks` saves read time, not space (fine at 58% full); keep it
    under ~80% full (GC). First check whether loading is disk-bound at all: time a game's load with a cold
    page cache vs right after.
    Prepared: `f2fs` in mkinitcpio MODULES (both images), `f2fs-tools` installed. Plan, from the Arch live USB
    over ssh (key: `mount -o ro /dev/disk/by-uuid/7a64638b-a129-47b5-a777-d0cd63d86cf1 /mnt && install -Dm600
    /mnt/home/robin/.ssh/authorized_keys /root/.ssh/authorized_keys`; Wi-Fi via `iwctl`):
    1. Back up mmcblk1p3 (29 GB) to the desktop: `tar --xattrs --acls --numeric-owner -cpS | zstd -1 -T4` over
       ssh (-c aes128-gcm: ~45 MB/s measured, 10-15 min); keep it as the rollback. Verify (list, size) first.
    2. `mkfs.f2fs -f -l root -U 7a64638b-a129-47b5-a777-d0cd63d86cf1 -O extra_attr,inode_checksum,sb_checksum,compression`
       (same UUID: boot entries and fstab's UUID stay valid).
    3. Mount with `compress_algorithm=lz4,compress_extension=*` and `nocompress_extension` for already-compressed
       types (chd, cso, zip, 7z, png, jpg, ogg, mp3, mp4, bik, ...); restore the tar (also ~10-15 min).
    4. fstab: ext4 -> f2fs with those options (plus `lazytime`); arch-chroot: `mkinitcpio -P` and
       `gpd-stock-initramfs` (fsck hook then picks fsck.f2fs); check `lsinitcpio`.
    5. Reboot; check Wi-Fi, sway, shelf, a game, suspend and hibernate (swap partition unchanged). Update
       boot.md/packages.md. Rollback: same steps with mkfs.ext4 and the backup.
    Later, for actual space savings: `f2fs_io release_cblocks FILE` frees the blocks compression saved; the file
    is then read-only (read, rename, delete fine; `f2fs_io reserve_cblocks` before any write, needs the space).
    Candidates: `/usr` (pacman replaces files by rename, so a periodic job re-releases new ones) and installed
    games (`scripts/games` releases after install, reserves before an update; skip saves/settings). Not ROMs
    (already compressed). Risk: Wine games may open data files read-write and fail on released ones: test per
    game, keep an exclude list. Estimate (unmeasured): 2-4 GB of the 15 GB games plus a few GB of `/usr`.
    Larger clusters (`compress_log_size`, up to 1 MB) compress better but amplify small random reads; keep LZ4
    (zstd shrinks more but decompresses far slower on this CPU).
15. Second device: ASUS ROG Ally (RC71L, Ryzen Z1 Extreme, Radeon 780M, 1080p 120 Hz VRR, no keyboard), same
    bare Arch + sway + shelf setup from this repo, installed and maintained over ssh like the GPD (discussed
    2026-10-12; Bazzite rejected: the point is our own minimal system).
    1. Pad-only control, built and tested on the GPD first (useful there too):
       - Gamepad shortcut layer in inputd: being designed in `docs/plans.md` ("Pad system layer").
       - shelf: a per-game settings panel on a pad button (Y) for what T/G/W/P/L do now.
       - On-screen keyboard `wvkbd` (layer-shell, small) on demand, for foot tools, search and Wi-Fi passwords.
       - Gamepad navigation in `scripts/games` (item 8).
    2. Per-device split (superseded by the layer plan in `docs/repo-layout.md`): a device column in `manifest.tsv` (by hostname); per-device sections where hardware is
       touched: `statusbar` (power limit, sensors), `game-wrap` and shelf's W steps (GPD 1-8 W via RAPL; Ally
       ~9-30 W via asus-wmi `ppt_pl1_spl`/`ppt_pl2_sppt`/`ppt_fppt` or `asus-armoury`), GPU clock pin (amdgpu),
       charge limit (Ally: `charge_control_end_threshold`), rotation and idle times. GPD-only: `devices/gpd-win1/mesa/`, `devices/gpd-win1/dxvk/`,
       the bq24190 charge-limit service.
    3. Ally specifics: its two extra buttons and back paddles come through `hid-asus` (map one to inputd's Menu
       action, one to a quick menu); stock Mesa (radv) and Proton, so Windows games can go through Steam
       instead of the GPD's Wine recipes; keep games on the internal SSD (the original Ally's SD reader is known
       to fail from heat).
