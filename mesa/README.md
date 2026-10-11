# Patched Mesa for the GPD (vendored Arch PKGBUILD)

Why: the DSI display flashes/splits on this unit are triggered by GPU uploads from Mesa's snooped staging
textures (`kernel/dsi-investigation/FINDINGS.md`, `reports/R4-upload-path.md`). Also a CPU cost: SDL's opengl
renderer uploads ~1 ms slower per frame on average and ~5 ms slower in at least 10 % of frames.

Contents:
- `PKGBUILD.arch`: Arch's PKGBUILD at tag `1-26.2.4-1` (https://gitlab.archlinux.org/archlinux/packaging/packages/mesa),
  `keys/`: its PGP keys. `PKGBUILD`: the same with `pkgrel=1.1` and our patches added (diff the two).
- `0001-crocus-don-t-snoop-upload-staging-textures.patch` (release 1.2): on non-LLC parts, staging textures that
  are only upload sources (sampler-view binding, or a write-only map in `crocus_map_copy_region`) are not snooped;
  readback staging stays as before.
- `unused/`: the test-switch patches of the 1.1 test build (fix 2 `MESA_ST_MATCH_SIZED`, fix 1a/1b
  `CROCUS_STAGING_NOSNOOP`, `CROCUS_SNOOP_UC_MOCS`), kept for reference, not built.
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

Readback (`kernel/dsi-investigation/scripts/readtime.c`, 853x480, median of 100): system Mesa glReadPixels(GL_BGRA)
4.87 ms, glGetTexImage(GL_BGRA) 5.54 ms; with *all* staging textures unsnooped (test switch) 3.08 / 3.76 ms (crocus
maps them cached and invalidates before the CPU reads; the BO is reused). So the scoping in the 1.2 patch is not
needed for performance: an unscoped "no snooping for staging textures on non-LLC" would be simpler and faster in
both directions (option for the final version / upstream).

Final 1.2 test (2026-10-11 23:56-00:11, `kernel/dsi-investigation/scripts/finaltest.sh`, Mesa from /tmp): colour
check PASS; burst upload-only 10 min **0 events**, upload mean 1.14 ms, never > 5 ms (stock: 205 events, 3.73 ms,
20 % > 5 ms); burst upload+draw 5 min 0 events. Readback unchanged (alternating runs, glReadPixels GL_BGRA median
system 5.66 / 4.83 ms vs 1.2 4.94 / 4.72 ms; GL_RGBA 4.5 ms both).
