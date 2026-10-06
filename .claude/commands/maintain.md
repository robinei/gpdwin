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
   tell the user a reboot is needed (don't reboot yourself).

4. **AUR packages.** `scripts/aur check`. For each package with an update:
   - `scripts/aur review PKG` and read the diff yourself. Flag: new or changed source URLs or
     hosts, checksum changes without a version change, `SKIP` checksums on non-VCS sources,
     downloads or `curl|sh`/`eval` inside `build()`/`package()`/`prepare()`, new `.install`
     scripts or hooks, files written outside `$pkgdir`, new dependencies, a maintainer change
     (compare with the reviewed row in `aur/reviewed.tsv`), obfuscated code.
   - Give the user a short verdict (what changed, anything suspicious) and ask before building.
   - `-git` packages build whatever upstream HEAD is; say so, and look at the upstream commit log
     since the installed version when it's cheap to do.
   - Build with `scripts/aur build PKG --yes` (it records the reviewed commit and a changelog line).
     Never use `yay -S`/`yay -Syu` for AUR builds here; yay can't build pegasus-frontend-stable-git
     anyway (its PKGBUILD echoes at top level).
   - After updating `pegasus-frontend-stable-git`, tell the user to restart Pegasus.

5. **pacnew/pacsave.** `pacdiff -o`. For each, diff against the live file, merge sensibly (keep
   our changes, take upstream's new defaults), install the result with sudo, delete the .pacnew,
   and `scripts/sync capture <file>` if it is in `manifest.tsv`.

6. **Housekeeping.** Orphans: `pacman -Qdtq` (list, ask before removing). Disk: `df -h /` and
   `du -sh ~/.cache/*`. Failed units: `systemctl --failed`. Recent errors: `journalctl -b -p err`
   (compare with known-harmless messages in docs/hardware.md).

7. **Record.** Append a dated entry to `docs/changelog.md` (what was updated, anything notable),
   update other docs if facts changed, `git add -A && git commit`. Summarize for the user, and
   mention anything that needs their action (reboot, Pegasus restart, manual BIOS step).
