---
description: Install a new AUR package the vendored, reviewed way (scripts/aur)
argument-hint: <package name or what the user wants>
---

The user wants a new AUR package: $ARGUMENTS

Prefer the official repos: check `pacman -Ss` first, and only use the AUR if nothing official
fits. Keep the AUR set small (docs/packages.md "AUR policy").

1. **Pick the package.** `scripts/aur search <term>`. Compare candidates: `-bin` (upstream
   binaries, quick) vs source builds (slow on this Atom) vs `-git` (unpinned upstream HEAD).
   Check votes, last update, out-of-date flag, maintainer. Recommend one and say why.
2. **Review.** `scripts/aur review PKG` shows every file of a never-reviewed package. Read all
   of it. Check source URLs point at the official upstream, checksums are pinned (no `SKIP` on
   non-VCS sources), `.install` scripts and hooks, nothing written outside `$pkgdir`, and
   whether the binary is 32-bit (no multilib here; see docs/frontend.md). For `-bin` packages,
   download the upstream release yourself and compare its sha256 with the PKGBUILD.
3. **AUR dependencies.** `scripts/aur build` only installs repo deps. If the package needs other
   AUR packages, add those first with this same procedure.
4. **Our changes, if needed** (ask the user first), as patches rather than edits:
   - `aur/patches/PKG/pkgbuild/NNNN-*.patch` for the build files: missing deps, pinning a
     `-git` source (`#commit=<full sha>`), `backup=(...)` for configs we customize, tolerant
     `.install` scripts.
   - `aur/patches/PKG/NNNN-*.patch` for the source (`target` file names the dir under src/ if
     there is more than one).
   - Files that must not be in git (game data, ROMs): `~/.local/share/gpd/aur-local/PKG/`.
   Dry-run each patch (`patch -p1 --dry-run`) before building.
5. **Build and install.** `scripts/aur build PKG --yes` (detached for long builds, see
   /maintain). It vendors the build files into `aur/pkgbuilds/PKG`, records the review in
   `aur/reviewed.tsv` and adds a changelog line. Set a meaningful note in `reviewed.tsv`
   (how thoroughly it was reviewed).
6. **Integrate and test.** Run it once if possible. Add it where it belongs (Pegasus entry,
   sway binding, manifest entry for any config), update docs/packages.md (AUR set) and the
   topic doc, then `scripts/sync check`, commit and push.
