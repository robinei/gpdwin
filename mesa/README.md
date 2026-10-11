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

Test results 2026-10-11 (GPD, `kernel/dsi-investigation/scripts/mesatest.sh`: burst `-m upload -d 10`, SDL opengl
renderer, 10 min each, Mesa from /tmp via LD_LIBRARY_PATH; colour check `colorcheck.c` PASS in every run):

| switches | flash events | upload mean | uploads > 5 ms |
|---|---|---|---|
| none (= Arch behaviour) | 205 | 3.73 ms | 20.3 % |
| MESA_ST_MATCH_SIZED=1 (fix 2) | 0 | 2.64 ms | 0 % |
| CROCUS_STAGING_NOSNOOP=1 (fix 1a) | 0 | **1.14 ms** | 0 % |
| CROCUS_SNOOP_UC_MOCS=1 (fix 1b) | 3 | 3.42 ms | 9.3 % |
| fix 2 + fix 1a | 0 | 2.73 ms | 0 % |

Conclusion: the snooped staging BO is the trigger; not snooping it removes the flashes and makes the upload 3x
cheaper (the BO becomes reusable: no per-frame create/clflush). Fix 2 is safe but makes the CPU do the tiled copy
(slower than fix 1a). Plan: fix 1a by default on non-LLC, scoped to upload staging (CPU readback staging stays
snooped); fix 2 not by default. Not installed on the GPD yet.
`lib32-mesa` (Wine's 32-bit games) is not done yet.
