---
description: Routine maintenance of the GPD Win 1 - news, updates with AUR review, pacnew, drift, changelog
---

Run a maintenance pass on this device. The user started this from Pegasus (`scripts/maintain`),
so sudo works non-interactively (`sudo -n ...`) for the duration of the session. Keep the user
informed in short lines; they are on a small screen with a keyboard.

1. **Prepare.** `git status` in ~/gpd. If there are uncommitted changes, show them and ask
   whether to commit them first. Run `scripts/sync check`; report any drift and ask how to resolve
   it (capture live into the repo, or install the repo version) before going on.

2. **Arch news.** Run `scripts/news 8`. For any item newer than the last update in
   `docs/changelog.md` that mentions manual intervention or a package installed here
   (`pacman -Qq`), open the link, summarize what to do, and do it (or ask) before upgrading.

3. **Repo packages.** `checkupdates` to list pending updates; summarize (note kernel, mesa,
   systemd, sway, pipewire, firmware). Then `sudo -n pacman -Syu --noconfirm`. Read the output for
   warnings, failed hooks and `.pacnew` notices. If the kernel or `linux-firmware-*` changed,
   tell the user a reboot is needed (don't reboot yourself). If the kernel changed: our patched audio modules (`kernel/`, DKMS package `gpd-audio`,
   docs/hardware.md "Audio") are rebuilt for the new kernel by the dkms pacman hook. Read the pacman
   output for DKMS build errors and run `dkms status` (should list `gpd-audio/1.0, <new kernel>,
   x86_64: installed`). If the build failed the stock modules are used (sound works, the hibernate
   bug is back): update the pins in `kernel/*/sources.sha256` and check that the patches still apply
   (`kernel/prepare.sh`). Check whether the patches have been merged upstream first (then drop them).
   Patched i915 (docs/hardware.md "Display", "Current state"): it is a prebuilt module for one kernel
   version, built on the desktop (`kernel/i915/build.sh`, version and pins hard-coded). Before a kernel
   change the pacman hook makes the stock entry `arch.conf` the default again (check: `bootctl status`
   shows it). Tell the user the display fix is off until the module is rebuilt for the new kernel on
   the desktop (update V/KV in build.sh and the pins in sources.sha256), reinstalled with
   `install-test.sh` and made the default again (`bootctl set-default arch-i915test.conf`).

4. **AUR packages.** `scripts/aur check`. Our build files are vendored in `aur/pkgbuilds/PKG/`
   and our changes live in `aur/patches/PKG/` (`pkgbuild/*.patch` for build files, `*.patch` for
   the source); see docs/packages.md "AUR policy". For each package with an update:
   - `scripts/aur review PKG` and read the diff yourself. Flag: new or changed source URLs or
     hosts, checksum changes without a version change, `SKIP` checksums on non-VCS sources,
     downloads or `curl|sh`/`eval` inside `build()`/`package()`/`prepare()`, new `.install`
     scripts or hooks, files written outside `$pkgdir`, new dependencies, a maintainer change
     (compare with the reviewed row in `aur/reviewed.tsv`), obfuscated code.
   - Give the user a short verdict (what changed, anything suspicious) and ask before building.
   - `scripts/aur check` also lists every git source pinned with `#commit=` and how many upstream
     commits are newer (pins come from our patches, e.g. zelda3-git, or from the AUR PKGBUILD
     itself, e.g. pegasus-frontend-stable-git). Mention notable gaps to the user.
   - Pinned `-git` packages (a pin patch in `aur/patches/PKG/pkgbuild/`, e.g. zelda3-git) don't
     move with the AUR. To update one, look at the upstream commits since the pin, summarize
     them, and on approval bump the commit in the pin patch. Unpinned `-git` packages build
     whatever upstream HEAD is: say so, and propose pinning them.
   - Build with `scripts/aur build PKG --yes`. It applies our patches to pristine sources, keeps
     sudo alive, installs, records the reviewed commit, re-vendors `aur/pkgbuilds/PKG`, adds a
     changelog line, cleans the build dir, and commits + pushes that record by itself
     (so `git pull` first if you have unrelated uncommitted work). Long builds (Pegasus ~20 min): run it detached
     (`setsid -f bash -c "scripts/aur build PKG --yes > /tmp/PKG-build.log 2>&1 < /dev/null"`)
     and poll the log.
   - **A patch fails to apply** (upstream changed the same lines): don't drop it. Fetch the new
     source (`makepkg -o` in `~/.cache/gpd-aur/PKG` after the build-file patches), redo the
     change by hand, regenerate the patch with `diff -u` (paths `a/...` and `b/...`, `-p1`
     relative to the target dir), check it with `patch -p1 --dry-run`, and say what changed.
     If upstream fixed the problem itself, delete the patch and note it in the changelog.
   - After the build, check what each patch was for still holds (e.g. Pegasus idle: 0 frames/s,
     see docs/frontend.md), and that `git diff aur/pkgbuilds` matches what you reviewed.
   - AUR packages are only built with `scripts/aur` (yay is not installed).
   - After updating `pegasus-frontend-stable-git`, tell the user to restart Pegasus.

5. **pacnew/pacsave.** `pacdiff -o`. For each, diff against the live file, merge sensibly (keep
   our changes, take upstream's new defaults), install the result with sudo, delete the .pacnew,
   and `scripts/sync capture <file>` if it is in `manifest.tsv`.

6. **Housekeeping.** Orphans: `pacman -Qdtq` (list, ask before removing). Disk: `df -h /` and
   `du -sh ~/.cache/*`. Failed units: `systemctl --failed`. Recent errors: `journalctl -b -p err`
   (compare with known-harmless messages in docs/hardware.md).
   Firewall loaded: `sudo -n nft list ruleset` shows `table inet filter` with `policy drop`.
   Audio paths still intact: `pacman -Q pipewire-pulse pipewire-alsa`, `pactl info` shows
   "PulseAudio (on PipeWire)", and `/etc/alsa/conf.d/99-pipewire-default.conf` exists.

7. **Record.** Append a dated entry to `docs/changelog.md` (what was updated, anything notable),
   update other docs if facts changed, `git add -A && git commit`. Summarize for the user, and
   mention anything that needs their action (reboot, Pegasus restart, manual BIOS step).
