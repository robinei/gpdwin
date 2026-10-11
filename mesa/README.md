# Patched Mesa for the GPD (vendored Arch PKGBUILD)

Why: the DSI display flashes/splits on this unit are triggered by GPU uploads from Mesa's snooped staging
textures (`kernel/dsi-investigation/FINDINGS.md`, `reports/R4-upload-path.md`). Also a CPU cost: SDL's opengl
renderer uploads ~1 ms slower per frame on average and ~5 ms slower in at least 10 % of frames.

Contents:
- `PKGBUILD.arch`: Arch's PKGBUILD at tag `1-26.2.4-1` (https://gitlab.archlinux.org/archlinux/packaging/packages/mesa),
  `keys/`: its PGP keys. `PKGBUILD`: the same with `pkgrel=1.1` and our patches added (diff the two).
- `0001-st-mesa-sized-formats-in-upload-order.patch` (fix 2): GL_RGBA8 + GL_BGRA data is stored as BGRA so uploads
  are a plain copy. **Test switch: only with `MESA_ST_MATCH_SIZED=1`.**
- `0002-crocus-staging-snoop-switches.patch` (fix 1 candidates): `CROCUS_STAGING_NOSNOOP=1` (staging BOs not
  snooped), `CROCUS_SNOOP_UC_MOCS=1` (snooped BOs read with the uncached MOCS). **Test switches, default off.**
  With no variable set, this Mesa behaves like Arch's.
- `build.sh`: builds all packages of the pkgbase on the desktop in a clean chroot (devtools, sudo).

Status 2026-10-11: test build; switches decide which fixes become the default. Not installed on the GPD.
`lib32-mesa` (Wine's 32-bit games) is not done yet.
