# Patched Mesa for the GPD (vendored Arch PKGBUILD)

Why: the DSI display flashes/splits on this unit are triggered by GPU uploads from Mesa's snooped staging
textures (`kernel/dsi-investigation/FINDINGS.md`, `reports/R4-upload-path.md`). Also a CPU cost: SDL's opengl
renderer uploads ~1 ms slower per frame on average and ~5 ms slower in at least 10 % of frames.

Contents:
- `PKGBUILD.arch`: Arch's PKGBUILD at tag `1-26.2.4-1` (https://gitlab.archlinux.org/archlinux/packaging/packages/mesa),
  `keys/`: its PGP keys. `PKGBUILD`: **slim GPD build** (release 1.3): only crocus + softpipe (OpenGL/GLES, EGL, GLX,
  GBM) and hasvk (Vulkan), packages `mesa`, `vulkan-intel`, `vulkan-mesa-implicit-layers` (what the GPD has
  installed). No LLVM/SPIRV-Tools/lm_sensors at runtime: crocus and hasvk need OpenCL C kernels compiled at build
  time, so build() first builds the host tools `mesa_clc`/`vtn_bindgen2` with LLVM (stage 1, plain meson), then the
  drivers with `-D mesa-clc=system -D llvm=disabled`. `lib32/`: the same for `lib32-mesa` (Arch's lib32 PKGBUILD in
  `lib32/PKGBUILD.arch`). When Arch updates: diff the new Arch PKGBUILD against `PKGBUILD.arch` and carry over what
  matters (version, sums, options), then build.
- `0001-crocus-don-t-snoop-staging-textures.patch` (since 1.4; 1.2/1.3 had a scoped version, now in `unused/`):
  no snooping for staging textures on non-LLC parts. Uploads through staging 3.73 -> 1.14 ms, readback with
  conversion ~5.4 -> ~3.3 ms, and it stops the DSI display flashes.
- `0002-crocus-blit-large-tiled-uploads.patch` (since 1.4): write-only uploads >= 64 KiB into tiled textures on
  non-LLC parts use a linear staging texture + GPU blit instead of the CPU tiled copy through an uncached map:
  matching-format uploads (SDL opengles2, wlroots shm clients) 2.01 -> 0.91 ms (853x480).
- `unused/`: the test-switch patches of the 1.1 test build (fix 2 `MESA_ST_MATCH_SIZED`, fix 1a/1b
  `CROCUS_STAGING_NOSNOOP`, `CROCUS_SNOOP_UC_MOCS`), kept for reference, not built.
- `build.sh`: builds all packages of the pkgbase on the desktop in a clean chroot (devtools, sudo).
  Uses `chroot-pacman.conf` (official Arch mirrors, own cache; the desktop is CachyOS). Takes ~25 min.
- Package names: `mesa-gpd`, `vulkan-intel-gpd`, `vulkan-mesa-implicit-layers-gpd` (+ `lib32-`), each providing
  and conflicting the Arch name, so `pacman -Syu` never replaces them; `scripts/aur check` compares them with the
  repo versions (REBUILD when Arch is newer).
- `install.sh PKG...` (GPD, sudo): saves Arch's installed packages to `/var/cache/gpd-mesa-stock`, installs ours
  (`--ask=4` removes the conflicting Arch package). `rollback.sh` (GPD, sudo): reinstalls the saved Arch packages.

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

## Slim build (1.3) measurements, 2026-10-12 (`mesa/bench.sh`, GPD, Mesa from /tmp)

| | full 1.2 (Arch build + patch) | slim 1.3 generic | slim 1.3 `-march=silvermont` |
|---|---|---|---|
| package `mesa` | 14.2 MB | 5.5 MB | |
| `libgallium` | 55 MB + libLLVM 172 MB + SPIRV-Tools | 27 MB, no LLVM/SPIRV | |
| GL program start→exit (colorcheck), warm / no shader cache | 94 / 105 ms | **62 / 71 ms** | 62 / 69 ms |
| 32-bit GL program start→exit (colorcheck32) | 102 ms | **67 ms** | |
| running GL program RSS / PSS | 102 / 82 MB | **39 / 34 MB** | 39 / 34 MB |
| mapped libraries | 50 | 38 | 38 |
| burst no vsync fps: upload / upload+draw / draw / Vulkan | 680 / 550 / 718 / 195 | 713 / 561 / 692 / 206 | 695 / 558 / 733 / 195 |
| readback glReadPixels/glGetTexImage | 4.9-5.3 ms | 5.2-6.0 ms | 4.6-6.0 ms (run-to-run spread ~1 ms) |
| build time (desktop) | ~25 min | ~3 min | |

`-march=silvermont` gave no measurable gain, so `_march`/`_mtune` stay Arch's generic values. `mesa_glthread=true`
(off by default for crocus) gave no measurable gain either (upload 707→758, full 555→545, static 689→691 fps;
draw-heavy variants were GPU fill-rate bound); not enabled. Functional checks with the slim build: colorcheck PASS
(64/32-bit, also softpipe via LIBGL_ALWAYS_SOFTWARE), GLX via Xwayland (SDL x11), Vulkan (SDL vulkan renderer).

## 1.4 (2026-10-12): candidates measured (test build with switches, `mesa/bench.sh`-style runs on the GPD)
- Uploads with matching formats (SDL opengles2, `burst -m upload -n`): CPU tiled copy 2.01 ms / 417 fps, GPU blit
  0.89 ms / 734 fps (threshold 64 KiB or 0: same). Upload+draw unchanged (texture busy: staging already).
- Readback (readtime, two alternating runs): snooped staging glReadPixels GL_BGRA 5.17 / 5.61 ms, unsnooped
  3.44 / 2.97 ms; glGetTexImage GL_BGRA 5.69 / 6.51 -> 4.32 / 3.62 ms; GL_RGBA (no staging) unchanged.
- Vertex/constant streaming: not snooped already (crocus_resource_create_for_buffer passes no flags), no change.
- Draw-call overhead (`drawbench 2000`): 2.9 ms/frame = ~690k draws/s; glthread 3.0 ms (no gain).
- Final 1.4 from /tmp: colorcheck PASS (64/32-bit); opengles2 upload 0.91 ms / 699 fps, opengl 0.82 ms / 757 fps;
  0 flash events in 5 min of the SDL-style upload test; readback GL_BGRA 3.55 / 4.35 ms. Installed 2026-10-12.

## 1.5 (2026-10-12, installed)
Adds R7's patches 0003 (upload-blit threshold 256 KiB), 0004 (read-only maps of tiled textures >= 64 KiB via staging
blit: glReadPixels/glGetTexImage without conversion ~5 -> 3.9 ms, 1280x720 ~11 -> ~5 ms; glGetTexImage *with*
conversion ~1 ms slower), 0005 (read-only buffer maps invalidate only the mapped range: 0.8-1.4 -> 0.11 ms for 64 B
from 4 MiB) and the driconf trim (`drirc-trim.py`, `drirc-keep.txt`: 111 native-program entries dropped, all .exe
entries kept; ~3-4.5 ms faster GL program start). Correctness review R6: all ship; dEQP 12,994 cases and piglit
identical to stock. From /tmp before install: colorcheck PASS 64/32-bit, 0 flash events in 5 min, frame rates unchanged.
To keep a driconf workaround for a newly installed native game: add its executable to `drirc-keep.txt`, rebuild.
