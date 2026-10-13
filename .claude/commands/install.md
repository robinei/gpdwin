---
description: Install Arch with this repo's setup on a handheld booted from the Arch install medium, over ssh - argument: root@<ip of the live system>
---

Install a complete system on the device at `$ARGUMENTS` (the Arch install medium, reached over ssh), using this
repo's layers (docs/repo-layout.md): the device's fixes, the components the person wants, and a user layer.
You run on another computer; every command goes over `ssh $ARGUMENTS`. The person holds the device: keep
messages short and say when they need to do something on it.

**Never guess on the disk.** Wiping it is the one step that can't be undone: show what is on it and get a
clear yes for that disk before partitioning. Passwords are typed by the person, never by you.

## 0. What the person does first (tell them if ssh doesn't answer)
On the device, booted from the Arch ISO: `iwctl station wlan0 connect "<network>"` (or a USB Ethernet
adapter), `passwd` (a temporary root password for ssh), `ip -4 addr` for the address. The ISO's sshd
accepts root with that password. Then copy your ssh key over (`ssh-copy-id`) so they type it once.

## 1. Which device is it
- Read: `cat /sys/class/dmi/id/{sys_vendor,product_name,board_vendor,board_name,bios_version,bios_date}`,
  `lscpu | head -20`, `lspci -nn`, `cat /proc/bus/input/devices | grep Name`, `ls /sys/class/power_supply`,
  `cat /sys/class/drm/*/modes`, `lsblk -o NAME,SIZE,MODEL,TRAN,TYPE,FSTYPE,LABEL`.
- Compare with each `devices/*/README.md` ("How to recognise it"). A match: that is `device=`. None: this is a
  new device; do `/port-device` instead (it starts from here too) and come back.

## 2. Choices (ask once, all together; defaults in brackets)
- Components [all: `base desktop inputd shelf`]. `base` is always needed; `desktop` = the sway session;
  `inputd` = idle/lid/power-button policy and the menu key; `shelf` = the game frontend (needs desktop).
  `none` = only the device fixes (with `user=none`): they bring their own desktop; still install the
  packages in the device's `packages.txt` and a bootloader the device layer expects.
- User name, host name, time zone, Wi-Fi country (for `cfg80211`), keyboard layout if not US.
- Their user layer: start from `users/example` (copied to `users/<name>`), or an existing one (path or repo).
- Which target disk (from `lsblk`; the device README says which is internal) and whether to wipe it all.

## 3. Disk (after the explicit yes)
Use the device README / `docs/boot.md` layout if it has one; otherwise: GPT, 512 MiB EFI system partition
mounted at `/boot` (FAT32), swap = RAM size if they want hibernation, the rest ext4 `/`. Mount at `/mnt`.

## 4. Base system
- `pacman -Sy --needed git` on the live system, clone this repo to `/root/gpd`, write a host file for the
  install (`/root/.config/gpd/host`: `device=`, `user=`, `components=`) so `scripts/sync packages` and
  `services` resolve the layers: `HOME=/root /root/gpd/scripts/sync packages`.
- `pacstrap -K /mnt $(that list)`. Multilib packages (lib32-*) need `[multilib]` in the live system's
  `/etc/pacman.conf` first (the repo's `system/etc/pacman.conf` has it).
- `genfstab -U /mnt >> /mnt/etc/fstab`; in `arch-chroot /mnt`: time zone, `hwclock --systohc`, locale
  (`en_US.UTF-8` unless asked), hostname, `bootctl install`.
- The user: `useradd -m -G wheel,seat,realtime -s /usr/bin/fish <name>`; the person sets the password:
  tell them to run `arch-chroot /mnt passwd <name>` on the device (or over your ssh with `!` in their own
  terminal). Add their ssh public key to `/mnt/home/<name>/.ssh/authorized_keys` (mode 600, owned by them).

## 5. The repo, as the user (inside the chroot)
- `git clone <this repo> /home/<name>/gpd` (the path scripts expect), owned by them. Their user layer at
  `users/<name>` (or `user_dir=`). Host file `/home/<name>/.config/gpd/host`.
- `scripts/sync install` needs `sudo -n` for system files: in the chroot, run it as the user with a
  temporary `/etc/sudoers.d/99-install` (`<name> ALL=(ALL) NOPASSWD: ALL`, mode 440) and **delete that file
  afterwards**. Then `sudo mkinitcpio -P` and `systemctl enable` each line of `scripts/sync services`
  (`user X` lines: `systemctl --global enable X` or as the user after first login).
- `sudo pacman -S` anything `sync install` reports missing (e.g. a build needs a library).

## 6. Device extras and AUR
- The device README's "parts that don't come from pacman" (kernel modules, Mesa, ...): offer them; they can
  also come later (`/maintain`). Kernel modules need the installed system booted (DKMS), so after step 7.
- AUR packages the user layer lists: only through `scripts/aur` (review first; CLAUDE.md), after the first boot.

## 7. First boot
Unmount, reboot, remove the USB stick. The device should autologin into sway (and shelf). Over ssh as the
user: `scripts/sync check` must say no drift. Record the install in `docs/changelog.md` (date, device,
components) and in the device's docs anything that differed; commit if the person wants their layer kept.
