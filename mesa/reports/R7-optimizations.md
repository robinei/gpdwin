# R7: new Mesa optimization opportunities for the GPD Win 1 (Cherry Trail, crocus) — 2026-10-12

Scope: measure what Mesa 26.2.4 (our 1.4 build: slim crocus + hasvk, patches 0001 no snooped staging, 0002 GPU blit for
uploads >= 64 KiB) does badly on this device (Atom x7-Z8700, Gen8 LP, no LLC, 200-600 MHz GPU), prototype fixes behind
environment switches, measure. Everything ran on the GPD from `/tmp/r7` (test Mesa builds via `LD_LIBRARY_PATH`, nothing
installed, no system changes) under `flock /tmp/gpd-bench.lock`; the kernel is 7.2.9, `mesa-gpd 1:26.2.4-1.4` installed.
Builds: `1.7` = 1.4 + test switches (`r7/r7-test-switches.patch`, built and measured), `1.9` = 1.4 + the three
clean patches `r7/0003..0005-*.patch` (built and validated against the installed 1.4, see section 5; a `1.8` build in
between had only patch 0005 because of a PKGBUILD mistake and is not used here). Benchmark sources are in `mesa/reports/r7/`
(`composite.c`, `texbench.c`, `bufbench.c`, `clf.c`, `ioctlshim.c`, `segv.c`, `planefmt.c`).
Legend: **[M]** measured, **[S]** source-verified (file:line in the Mesa 26.2.4 tree), **[H]** hypothesis.

The GPU clock matters for every GPU-side number below (section 2.1): in loops that wait for each frame the GPU stayed at
**200 MHz**, in pipelined loops at **600 MHz**. Each table says which. CPU-side numbers do not depend on it.

## 0. Summary, ranked by measured gain for things that run on this device

| # | finding | gain [M] | status |
|---|---|---|---|
| 1 | **Readback of tiled textures through a staging blit** (crocus detiles with the CPU from an uncached map when the format matches) | glReadPixels GL_RGBA 1280x720 **10.5-11.6 -> 4.8-6.1 ms**, 853x480 6.1-6.4 -> 3.2-3.9, 640x480 4.9 -> 2.7, 320x240 1.8-1.9 -> 1.2-1.3; glGetTexImage RGBA 853x480 5.1-5.3 -> 3.8-3.9. Data byte-identical. **One measured regression**: glGetTexImage with a conversion (GL_BGRA from RGBA8, 853x480) 4.1-4.5 -> 5.2 ms (section 5). | patch `r7/0004`, built (1.9), validated; decide with the regression in mind |
| 2 | **Read-only buffer maps invalidate only the mapped range** (crocus clflushes the whole BO: 7.6 ns/line = 0.5 ms per 4 MiB) | glGetBufferSubData / glMapBufferRange(READ) of 64 B from a 4 MiB VBO **0.9-1.4 -> 0.11 ms**. Data byte-identical. | patch `r7/0005`, built (1.9), validated |
| 3 | **Upload-blit threshold 64 KiB -> 256 KiB** (our 1.4 patch 0002 sends everything >= 64 KiB through staging + blorp; the CPU tiled copy is cheaper up to ~256 KiB) | 16 x 64 KiB tile uploads/frame **2.7-3.0 -> 1.6 ms CPU**, 16 x 144 KiB 2.9-3.0 -> 2.2-2.6; 256 KiB equal CPU, less GPU; >= 400 KiB blit stays better | patch `r7/0003`, built (1.9), validated |
| 4 | **driconf XML parsing at every GL process start** (`00-mesa-defaults.conf`, 164 application entries, parsed twice: 20 % of user time) | process start **51 -> 44.5 ms real, 27 -> 20 ms user** (median of 15) with a minimal `DRIRC_CONFIGDIR` | not a Mesa patch: ship a trimmed defaults file in `mesa-gpd` or set `DRIRC_CONFIGDIR` (section 3.4) |
| 5 | **GPU clock policy (kernel RPS, not Mesa)**: the GPU stays at 200 MHz unless > 95 % busy; a sway-like composite costs 3.6 ms of GPU time at 200 MHz vs 1.4 ms at 600 | all GPU-side numbers x2.6; the 60 Hz compositor emulation measured at 200 MHz | finding for the main session (sysfs `gt_min_freq_mhz`/boost, battery trade-off), section 2.1 |
| 6 | **PBO texture uploads** (wined3d dynamic textures) go through st's shader path: 3-4x the GPU time of a blorp blit | 1280x720 BGRA from a PBO: GPU **5.9-6.2 ms** (200 MHz) vs 1.6-2.5 ms with the shader path disabled, but +1.1 ms CPU (map + copy) | trade-off, not proposed as is; idea: blit from the PBO's BO (section 3.6) |
| 7 | Composite (sway-like) facts: client buffer tiling matters (linear 3.0 ms vs Y-tiled 1.4 ms per frame at 600 MHz); external-buffer MOCS makes no difference; a sway-like frame costs ~0.45 ms user + 0.5 ms sys CPU, 13 ioctls | — | measurements, section 2 |

Measured and **not** worth doing: lowering the upload-blit threshold (worse), using the internal MOCS for external buffers
(no change), disabling CCS/fast clears (the fast clear is a clear win: 1.1 vs 4.3 ms), skipping the `SET_DOMAIN` prefault
at BO allocation (it only moves the page-allocation cost), persistent/unsynchronized buffer paths (already fine).

## 1. Method

- `composite.c`: GLES2 on a GBM device like wlroots: 720x1280 XRGB8888 scanout BOs (two, alternating; dmabuf-imported
  into renderbuffer FBOs), a 1280x720 client buffer (dmabuf with a chosen modifier, or a wl_shm-style texture updated
  with `glTexSubImage2D(GL_BGRA_EXT)`), per frame clear + rotated fullscreen window draw + blended bar, `glFlush`, EGL
  native fence like wlroots' explicit sync. Modes: synchronous (wait per frame), `-p N` pipelined (N frames in flight,
  whole-run throughput), `-w ms` (60 Hz emulation), GPU time from `EXT_disjoint_timer_query`, result check of a pixel.
- `texbench.c` (desktop GL, surfaceless): texture upload/readback/clear/mipmap/PBO patterns per frame, CPU time until the GL
  calls return and total with `glFinish`; `-t` GPU timestamps; `-o` dumps the readback for byte comparison; data checks.
- `bufbench.c`: buffer update patterns (glBufferSubData, orphaning, unsynchronized/invalidating maps, persistent coherent
  maps, uniforms, client arrays).
- `clf.c`: clflush cost; `ioctlshim.c`: LD_PRELOAD logging of GEM ioctls (sizes, durations); `perf record -e cpu-clock:u`
  with the 1.4 debug symbols (`/tmp/r5/mesa14`); `strace -c/-r/-T`; `INTEL_DEBUG=bat`.
- Spread: medians of 60-1500 frames, two alternating A/B runs for every switch comparison (both shown).

## 2. Baseline measurements (installed Mesa 1.4)

### 2.1 GPU clock [M]

`gt_act_freq_mhz` sampled during runs (RP0 600, RPn 200, min 200, boost 600):

| workload | GPU busy | act freq |
|---|---|---|
| `composite -c dmabuf` synchronous (submit, wait, repeat; ~75 % busy) | 75 % | **200** (6 samples over 4 s) |
| `composite -w 15` (60 Hz compositor emulation) | ~20 % | **200** |
| `composite -p 2` (pipelined, GPU never idle) | 100 % | **600** |
| `burst -m static -n` (GPU-bound SDL) | 100 % | 600 |

[S] `kernel/i915/build/src/drivers/gpu/drm/i915/gt/intel_rps.c:2040-2043`: up threshold 95 %, down 85 %, and
`rps_set_power` LOW_POWER mode at the minimum frequency with a 16 ms evaluation interval; `i915_request.c:2047` waitboost
only for `I915_WAIT_PRIORITY` waits on requests that have not started (crocus' fence waits are `SYNCOBJ_WAIT` =
`dma_fence_wait`, no boost). So in normal play (compositor + a game that does not saturate the GPU) the GPU runs at
200 MHz and every GPU-side cost is ~2.6x what it is at 600 MHz: the sway-like composite below is 3.6 ms instead of 1.4 ms
of the 16.7 ms frame. Mesa cannot change this; it is a kernel policy (sysfs `gt_min_freq_mhz`, needs root, costs battery).
All synchronous benchmarks in this report ran at 200 MHz, the pipelined ones at 600 MHz.

### 2.2 Compositor frame (what sway does every frame on the rotated panel) [M]

`composite`, 720x1280 X-tiled output (what `gbm_bo_create` with SCANOUT gives on crocus: `crocus_resource.c:73-89`
`modifier_is_supported`: Y-tiled is refused for scanout), client buffer from GBM with the default modifier (= Y-tiled,
the best non-scanout modifier `select_best_modifier` picks, which is also what EGL clients get through wlroots' dmabuf
feedback: the renderer's formats are LINEAR/X/Y):

| frame contents | pipelined (600 MHz) throughput / GPU timestamps | synchronous (200 MHz) frame / GPU |
|---|---|---|
| clear + window (Y-tiled dmabuf) + bar | 1.43-1.49 ms / 1.37 ms | 5.0 ms / 3.6 ms |
| no clear (sway with an opaque fullscreen window) + bar | 0.64 / 0.60 | — |
| window only | 0.61 / 0.56 | — |
| client buffer X-tiled | 1.71-1.74 / 1.64-1.67 | — |
| client buffer **linear** | **3.08 / 3.03** | — |
| output buffer linear | 3.33 / 3.28 | — |
| shm client, full 1280x720 damage (`glTexSubImage2D` BGRA, 3.7 MB) | 2.22 ms (CPU-bound: 2.17 ms) | 8.1 ms (CPU 3.4) |
| shm client, 64 rows damage | 1.52 | — |
| shm client through a PBO (not what wlroots does) | 6.27 | 10.9 |

CPU per frame (synchronous, `perf stat`): clear + window + bar 0.67 ms user + 0.46 ms sys; no clear 0.45 user + 0.53 sys.
Per frame 13 ioctls (`strace`): EXECBUFFER2, 4 MADVISE + 2 BUSY (the batch and state BOs come from the BO cache each
flush: `crocus_batch.c:460` + `crocus_bufmgr.c:300-330`), SYNCOBJ_CREATE/DESTROY, SYNCOBJ_HANDLE_TO_FD (the native
fence), SYNCOBJ_WAIT, 2 close; 4 `getpid` per frame (not identified). One batch per frame, 2 KB of commands, 9 BOs,
8 PIPE_CONTROLs. `perf` (user space) is flat: `crocus_upload_render_state` 4.8 %, `crocus_blorp_exec` 2.5 %, `ioctl`
2.2 %, `u_vbuf_draw_vbo` 2.1 %, `isl_gfx8_surf_fill_state_s` 1.4 %, nothing dominant. Idea (not prototyped): keep the
batch/state BOs instead of recycling them through the cache (saves ~6 ioctls = 50-100 µs per batch [H]).

### 2.3 Texture paths (`texbench`, desktop GL, 1280x720 RGBA8, synchronous = 200 MHz) [M]

| mode | CPU ms | frame ms | GPU timestamps ms | note |
|---|---|---|---|---|
| sub-rgba (`glTexSubImage2D` RGBA, matching: staging + blit since 1.4) | 1.9-2.2 | 4.8-5.2 | 1.9 | |
| sub-bgra (BGRA into RGBA8: staging + blit) | 2.4-2.6 | 6.1-6.4 | 2.5 | SDL opengl, Wine |
| pbo-rgba / pbo-bgra / pbomap-bgra (from a PBO) | 2.3-2.5 | **9.7-10.1** | **5.9-6.2** | st_pbo shader path, section 3.6 |
| subsmall: 16 x 64x64 RGBA tiles (16 KiB each, CPU tiled copy) | 1.28 (80 µs/tile) | 3.4 | | |
| create: new 1280x720 texture, clear, draw into, sample, delete | 0.41 | 3.8 | | BO cache absorbs it; CCS zeroing not visible |
| rtt / rtt-noclear (render to texture then sample) | 0.17 / 0.16 | 3.8 / 2.7 | | the clear + resolve = 1.06 ms |
| clear0 (fast clear, colour 0) + draw / clear5 (slow clear) + draw | 0.09 / 0.14 | **1.1 / 4.3** | 0.36 / 2.67 | fast clears are worth keeping |
| mipmap (glGenerateMipmap 1280x720) | 0.21 | 3.9 | | |
| readpixels RGBA (matching: CPU detiling from a WC map) | **10.4-11.4** | 10.7 | | section 3.1 |
| readbgra (conversion: staging blit + cached map) | 5.6-6.4 | 5.8 | | |
| readsmall 16x16 | 1.4-2.4 | | | dominated by the draw + wait |
| getbuf / mapread: 64 B from a 4 MiB VBO | **0.9-1.4** | | | section 3.2 |
| pboread (glReadPixels into a PBO + map) | 5.6 | | | |

### 2.4 Buffer paths (`bufbench`, 64 updates of 1 KiB per frame into a 1 MiB VBO, each followed by a draw) [M]

subdata 2.6 µs/update (CPU 0.16 ms/frame), mapunsync 4.5, mapinval 9.3 (new BO from the cache per map), persistent
coherent 2.0, uniforms 1.7, client arrays 3.0; orphaning with a 1 MiB `glBufferData(NULL)` + full `glBufferSubData`
309 µs (= the 1 MiB memcpy into WC at 3.4 GB/s); 16 x 64 KiB subdata into a busy buffer 43 µs each (staging copy + GPU
copy). Nothing abnormal; no change proposed.

### 2.5 Process start (`colorcheck`: EGL init, context, one draw, exit) [M]

50-53 ms real (25-28 user + 22-24 sys), 1322 page faults, 92 ioctls. `perf` over 25 starts, user time by DSO:
libgallium 28.6 % (of which the elk shader compiler for blorp's runtime-compiled shaders ~5 %), **ld.so 25.9 %**
(5639 relocations, `dlopen` of the 27 MB libgallium), **libexpat 20.5 %**, libc 19.3 %. The driconf files are parsed
twice per process (`xmlconfig.c:1336-1383` `driParseConfigFiles`, once for the loader and once for the screen):
00-mesa-defaults.conf (36 KB, 164 `<application>` entries), 00-crocus-defaults.conf, 00-hasvk-defaults.conf.
GEM: `SET_DOMAIN` after every fresh `GEM_CREATE` (`crocus_bufmgr.c:372-390`, the page prefault) costs ~1 ms per MiB
(1 MiB stream buffer 1.08 ms, 512 KiB 0.55 ms; 4 MiB texture 5-7 ms) — but this is the kernel allocating and zeroing the
pages, which would happen at first use anyway; skipping it was not pursued.

### 2.6 Cache maintenance cost [M] (`clf.c`)

`clflush` loop: 7.5-7.9 ns per 64-byte line whether the line is cached or not: 64 KiB 0.008-0.013 ms, 1 MiB 0.12 ms,
4 MiB 0.51 ms, 16 MiB 2.0 ms. No `clflushopt` on this CPU (cpuinfo: `clflush` only), so `util_flush_inval_range`
(`src/util/cache_ops_x86.c:111`) is a plain clflush loop + mfence.

## 3. Findings in detail

### 3.1 Readback of tiled textures: CPU detiling from an uncached map [S][M]

`crocus_transfer_map` (`crocus_resource.c:1600-1616`) sets `no_gpu = true` for every map that is not a discard
("GPU copies are not useful for buffer reads"), so a read-only map of a tiled texture whose format matches the
requested one (glReadPixels GL_RGBA from an RGBA8 FBO, glGetTexImage with the texture's format) goes to
`crocus_map_tiled_memcpy` (`:1440-1480`): `crocus_bo_map(bo, MAP_READ | MAP_RAW)` → `can_map_cpu` is false for
`MAP_RAW` (`crocus_bufmgr.c:1052-1057`) → **WC mapping**, then `isl_memcpy_tiled_to_linear` with streaming loads from
uncached memory: 1280x720 = 3.7 MB in 10.5 ms (350 MB/s). When a conversion is needed (GL_BGRA) the staging path is
taken instead: blorp blit to a linear staging texture, cached CPU map + invalidate (`crocus_bo_map_cpu` `:887-906`),
memcpy with swizzle: 5.6-6.4 ms. The matching-format case is the one that *should* be fastest and is 2x slower.

Fix (patch `0004`): on non-LLC parts, read-only maps (`PIPE_MAP_READ` without `WRITE`, not PERSISTENT/COHERENT/DIRECTLY)
of tiled textures of >= 64 KiB take the staging path (`crocus_map_copy_region` `:1150-1222`: blit, RT flush + CS stall,
flush, cached map). Measured (1.7 switch build, two alternating runs each, synchronous):

| | stock | read blit |
|---|---|---|
| glReadPixels RGBA 1280x720 | 10.47 / 10.58 ms | **4.72 / 4.79 ms** |
| glReadPixels BGRA 1280x720 (unchanged path) | 5.63 / 6.15 | 5.33 / 5.85 |
| glReadPixels RGBA 853x480 / 640x480 / 320x240 | 6.43 / 4.90 / 1.93 | **3.95 / 2.73 / 1.28** |
| `readtime` glReadPixels RGBA 853x480 / glGetTexImage RGBA | 5.20 / 4.73 | **3.93 / 3.88** |
| `readtime` BGRA (unchanged) | 3.45 / 4.18 | 3.49 / 5.31 (spread) |
| readsmall 16x16 (below the threshold, unchanged) | 2.38 | 2.40 |

Dumps of the 3.7 MB readback are byte-identical between the two paths (`cmp`), for RGBA and BGRA and 16x16.
Who uses it: screenshots (wlroots screencopy → `glReadPixels` from the X-tiled output buffer, grim), RetroArch
screenshots and save-state thumbnails, `SDL_RenderReadPixels`, wined3d `GetRenderTargetData`/lockable render targets,
Qt grabs. Not per-frame in normal play, but every such read is a visible stall halved.

### 3.2 Read-only buffer maps invalidate the whole BO [S][M]

`crocus_bo_map_cpu` (`crocus_bufmgr.c:887-906`) does `util_flush_inval_range(bo->map_cpu, bo->size)` on every map of a
non-coherent BO on non-LLC parts — the whole BO, whatever range is mapped. A 64-byte `glGetBufferSubData` from a 4 MiB
VBO costs the 65536 clflushes (0.5 ms, section 2.6) plus the draw and the fence wait.

Fix (patch `0005`): `crocus_bo_map_range()` invalidates `[offset, offset + size)` only; used by `crocus_map_direct` for
read-only buffer maps (the box is known there). Measured (two runs each): getbuf 1.28 / 0.87 → **0.11 / 0.11 ms**,
mapread 0.94 / 1.37 → **0.11 / 0.11 ms**; data identical (`cmp` of the 64 bytes, and the dump compare). A crash seen
during the first runs was a bug in `texbench` (uploading 4 MiB from a 3.7 MB array), not the patch: 0/50 failures after
the fix, with and without the switch. Who uses it: wined3d (vertex/index buffer locks with READ, rare per frame), any
`glMapBufferRange(GL_MAP_READ_BIT)` of a sub-range. Niche, but the fix is 30 lines and cannot be slower.

### 3.3 The 1.4 upload-blit threshold is too low [M]

`texbench subsmall` with 16 tiles per frame, threshold via the 1.7 switch `CROCUS_BLIT_UPLOAD_MIN` (1 MiB = CPU path for
all sizes here, 64 KiB = current 1.4 behaviour), CPU ms per frame (frame ms with glFinish), two runs:

| tile | bytes | CPU tiled copy (MIN=1M) | staging + blit (MIN=64K) |
|---|---|---|---|
| 128x128 | 64 KiB | **1.62 / 1.61** (3.7 / 4.0) | 2.98 / 2.67 (5.9 / 5.7) |
| 192x192 | 144 KiB | **2.18 / 2.63** (4.3 / 4.9) | 2.99 / 2.90 (6.0 / 6.2) |
| 256x256 | 256 KiB | 3.50 / 3.27 / 3.30 (**5.7 / 5.4 / 5.5**) | 3.54 / 3.52 / 3.36 (6.7 / 7.0 / 6.7) |
| 320x320 | 400 KiB | 6.58 (9.0) | **3.75-4.04** (7.3-7.9) |
| 384x384 | 576 KiB | 10.46 / 10.46 (12.8) | **4.33 / 4.14** (8.0) |
| 64x64, 64 tiles | 16 KiB | **2.08 / 2.70** | 3.6-4.0 (MIN 16K/4K/0) |

The crossover is between 256 and 400 KiB: the staging path has a fixed cost (staging allocation, blorp blit, a GPU pass)
that the CPU tiled copy beats below ~256 KiB, while the WC tiled copy falls off a cliff above (0.65 ms per 576 KiB tile).
Patch `0003` moves the threshold to 256 KiB. The 853x480 upload (1.6 MB) of DevilutionX/SDL stays on the blit path.

### 3.4 driconf parsing at process start [M][S]

`DRIRC_CONFIGDIR` (`xmlconfig.c:1364-1371`) pointed at a directory with only `00-crocus-defaults.conf` (557 bytes),
15 runs each, sorted:

| | user s | real s |
|---|---|---|
| stock `/usr/share/drirc.d` | 0.022 … **0.027** … 0.032 | 0.051 … **0.051** … 0.057 |
| minimal | 0.015 … **0.020** … 0.023 | 0.044 … **0.0445** … 0.051 |

−6.5 ms real (−13 %), −7 ms user per GL process start, for every game/frontend/Xwayland start. Options: (a) ship a
trimmed `00-mesa-defaults.conf` in `mesa-gpd` (keep the `<device>` global options and the entries for things that run
here: Wine executables, SDL/`wine-preloader` style entries, engines; the 164 entries are mostly Steam/Unity/Unreal game
names), (b) set `DRIRC_CONFIGDIR` globally with copies of the three files, trimmed. Both are packaging/config changes
for the main session, not Mesa code; (a) is cleaner. Risk: dropping an entry that a game here relies on (check the
Wine entries: `lycop.exe`, `JoeDanger*.exe`, `gunsound.exe`, `Blockland.exe`, `R6-Extraction.exe` etc. — none of the
installed games). Also [M] the dynamic loader is 26 % of start user time (`dlopen` of libgallium: 5639 relocations);
not pursued (it would need link-time changes with unclear gains: `-z now` is Arch's default and `-z lazy` measured
nothing in LD_DEBUG's own 0.3 ms startup figure — the cost is in the dlopen, which LD_DEBUG does not count).

### 3.5 GPU clock [M] — see 2.1. Not a Mesa change.

### 3.6 PBO texture uploads: the st_pbo shader path [S][M]

`st_pbo.c:648-651`: with texture buffer objects available, `glTexSubImage2D` from a bound `GL_PIXEL_UNPACK_BUFFER` is
done by `try_pbo_upload` (`st_cb_texture.c:1860-1960`): a fullscreen draw whose fragment shader `texelFetch`es the PBO
through a buffer sampler view. On this GPU that pass costs 3-4x a blorp blit of the same pixels:

| 1280x720 (200 MHz) | CPU ms | frame ms | GPU timestamps ms |
|---|---|---|---|
| pbo-rgba, st_pbo (stock) | 2.31 / 2.46 | 9.73 / 9.97 | **5.92 / 5.97** |
| pbo-rgba, `ST_PBO_UPLOAD=0` (map the PBO, ordinary upload = staging + blit) | 3.37 / 3.52 | 6.13 / 6.40 | 1.94 / 1.95 |
| pbo-bgra stock / disabled | 2.48 / 2.40 → 3.58 / 3.53 | 10.1 / 10.0 → 6.1 / 6.7 | 6.16 / 6.11 → 1.61 / 2.51 |
| pbomap-bgra (`glMapBufferRange` into the PBO, wined3d) stock / disabled | 2.27 / 2.36 → 3.60 / 3.33 | 9.7 / 9.6 → 7.1 / 5.8 | 6.01 / 5.88 → 2.51 / 1.61 |
| `composite -c shm -u -p 2` (600 MHz, pipelined) stock / disabled | 3.19 / 3.23 → 4.05 / 4.03 | 6.32 / 6.32 → 4.11 / 4.10 (throughput) | |

Disabling the shader path trades +1.1 ms CPU (the PBO is mapped: cached map + whole-BO clflush, then copied into the
staging texture) for −4 ms GPU at 200 MHz (−1.3 ms at 600). Not a clean win on a CPU-bound Atom, so not proposed.
Who hits it [S]: wined3d uses a PBO for D3D `DYNAMIC` textures and read-only staging textures
(`dlls/wined3d/texture.c` `wined3d_texture_use_pbo`, fetched 2026-10-12: `!(access & CPU) && usage & DYNAMIC` or
`access == CPU | MAP_R`), i.e. 2D games/video that lock a dynamic texture every frame; SDL, RetroArch, wlroots do not.
Idea with evidence: a crocus-side PBO upload that blits from the PBO's BO as a linear surface (what the staging path does
after its memcpy, without the memcpy and without st's shader pass) would be ~1.6-2.5 ms GPU and ~2.3 ms CPU (the
application's own copy into the PBO only) — better than both; it needs a new hook (st's `try_pbo_upload_common` draws, it
has no blit variant; `pipe->blit` cannot take a buffer). A day of work, worth it only if a Wine game with dynamic textures
is CPU-bound here. Why the st_pbo pass is 3x slower than blorp's sampler-based blit was not investigated.

### 3.7 Things measured and found fine or not worth changing [M]

- External (dmabuf) surfaces use the "external" MOCS (`crocus_resource.h:300-304` → `isl_mocs(external)`): the 1.7
  switch `CROCUS_EXT_MOCS_INTERNAL=1` changed nothing (composite 1.447/1.450 vs 1.461/1.458 ms; X-tiled client
  1.741 vs 1.721; shm 2.255 vs 2.336).
- CCS_D / fast clear on gen8: every RT-capable texture gets a CCS aux surface in the same BO, zeroed when the BO comes
  from the cache (`crocus_resource.c:478-497`, `crocus_bufmgr.c:335-343`, a WC memset of the whole BO). Measured cost
  invisible (`create` 0.41 ms CPU per 1280x720 texture churn), and the fast clear is a big win (clear0 1.1 ms vs clear5
  4.3 ms per frame; fast clear + resolve before sampling 1.06 ms). Keep.
- Lowering the upload-blit threshold below 64 KiB: worse (section 3.3).
- `SET_DOMAIN` prefault at BO creation: it is the page allocation itself (kernel zeroing), not waste.
- Buffer tiling on the compositor side [M] (`planefmt.c`): the KMS planes advertise exactly two modifiers,
  `I915_FORMAT_MOD_X_TILED` (0x100000000000001) and LINEAR, for XRGB8888, so wlroots' swapchain buffers are X-tiled
  (the measured "out=x" case, 1.38 ms; a linear output would cost 3.3 ms). EGL clients get Y-tiled buffers (crocus picks
  the best modifier from the compositor's LINEAR/X/Y list, `crocus_resource.c:95-120`), the measured best case. Nothing
  to fix, but worth knowing: a client that ends up with a linear buffer (e.g. a software-rendered dmabuf) costs sway
  2x per frame.
- Buffer streaming paths (section 2.4): nothing abnormal.
- Query buffers: `u_upload_create(.., PIPE_USAGE_STAGING)` buffers are not snooped on non-LLC (`crocus_resource_create_for_buffer`
  passes no flags) and are polled through a WC map (`crocus_query.c:640-646`): correct and cheap for the small struct.
- Composite CPU: flat profile, nothing to patch cheaply; the BO-cache ioctl churn per batch (6 ioctls) is the only
  candidate [H].

## 4. Prototype patches (in `mesa/reports/r7/`)

| file | what | state |
|---|---|---|
| `r7-test-switches.patch` | all switches: `CROCUS_BLIT_UPLOAD_MIN`, `CROCUS_READ_BLIT[_MIN]`, `CROCUS_INVAL_RANGE`, `CROCUS_EXT_MOCS_INTERNAL`, `ST_PBO_UPLOAD` | built as 1.7, measured above |
| `0003-crocus-raise-the-GPU-blit-upload-threshold-to-256-KiB.patch` | threshold 64 KiB → 256 KiB in the 1.4 patch 0002 logic | built as 1.9, validated (section 5) |
| `0004-crocus-read-back-tiled-textures-through-a-staging-bl.patch` | read-only maps of tiled textures >= 64 KiB via staging blit on non-LLC | built as 1.9, validated |
| `0005-crocus-invalidate-only-the-mapped-range-for-read-onl.patch` | `crocus_bo_map_range` for read-only buffer maps | built as 1.9, validated |

Applied on top of our 0001/0002 (the PKGBUILD used for 1.9 is the scratchpad `r7/mesa-r7final`, pkgrel 1.9, the
source list extended by the three patches, otherwise identical to `mesa/PKGBUILD`). The three clean patches have commit messages with the measurements.

Risks / correctness notes:
- 0004: the staging path is the one already used for every converting glReadPixels and for busy textures; the data
  compare is byte-identical; one measured slowdown (glGetTexImage with conversion, section 5); a READ|WRITE map keeps the old path (a WC-mapped staging would read slowly); ASTC and
  PERSISTENT/COHERENT/DIRECTLY maps are excluded as before. Cost: one more GPU pass per readback (the blit), which is
  what makes it faster overall; memory: a transient linear staging texture (BO cache).
- 0005: only the mapped range is invalidated; the caller may only touch `[box.x, box.x + width)` of the map (gallium
  contract). The fallback to `crocus_bo_map` keeps every other case (tiled BOs, writes, coherent BOs, LLC, out of range)
  unchanged. `can_map_cpu` needed a forward declaration.
- 0003: only the threshold constant; uploads of 64-256 KiB go back to the 1.3 (pre-0002) CPU path, which had no
  correctness or DSI-flash issue (the flashes came from snooped staging, fixed by 0001).

## 5. Validation of the 1.9 build (from `/tmp/r7/mesa19`, installed 1.4 as the stock comparison) [M]

Same machine state, alternating runs 1.4 / 1.9 (the 1.4 numbers are the installed package, so this includes any
run-to-run drift of the session; where several alternations were run all are listed).

| test | 1.4 | 1.9 | data |
|---|---|---|---|
| `colorcheck` | PASS | PASS (`Mesa 26.2.4-arch1.1.9`) | |
| composite dmabuf pipelined / shm (sanity) | 1.43-1.49 / 2.22 ms | 1.49 / 2.31 ms | pixel check ok |
| glReadPixels RGBA 1280x720 | 11.59 ms | **6.06** (p90 7.0; 4.7-4.8 in the 1.7 runs) | identical dump (3.7 MB) |
| glReadPixels RGBA 853x480 / 320x240 | 6.13 / 1.77 | **3.18 / 1.21** | |
| glReadPixels BGRA 1280x720 / 853x480 | 5.74 / 3.90 | 6.53 / 3.67 | identical |
| readsmall 16x16, getbuf, mapread | 2.29 / 0.80 / 0.82 | 2.65 / **0.107 / 0.109** | identical |
| `readtime` glReadPixels RGBA 853x480 (3 alternations) | 5.26 / 5.17 / 4.57 | **3.92 / 3.86 / 3.65** | |
| `readtime` glGetTexImage RGBA | 5.25 / 5.11 / 5.18 | **3.89 / 3.83 / 3.87** | |
| `readtime` glReadPixels BGRA (unchanged path) | 2.87 / 3.79 / 3.46 | 3.73 / 3.61 / 3.77 | |
| `readtime` **glGetTexImage BGRA** (conversion after the map) | 3.26 / 4.16 / 4.48 (4.07-4.18 earlier) | **5.18 / 5.25 / 5.18** (5.31 with the 1.7 switch) | regression, ~ +1 ms |
| subsmall 16 x 128x128 (64 KiB) tiles, CPU / frame | 2.02 / 2.06 ms, 4.8-5.0 | **1.48 / 1.52**, 3.7-3.9 | |
| subsmall 16 x 256x256 / 384x384 | 3.42 / 3.45, 4.20 / 4.44 | 3.54 / 3.52, 4.40 / 4.17 (same path) | |
| sub-rgba / sub-bgra 1280x720 (unchanged path) | 1.86 / 2.00 | 1.87 / 1.97 | |
| bufbench subdata / mapunsync | 2.4 / 3.2 µs per update | 2.3 / 2.9 | |
| `burst -m upload -n` (SDL opengl, on screen, 3 alternating 10 s runs) | 694 / 770 / 689 fps | 698 / 767 / 766 fps | no regression |
| `burst -m full -n` | 567 fps | 560 fps | |
| mipmap / rtt / create (1.9 only, sanity) | | 3.80 / 3.67 / 3.81 ms frame | |
| process start (15 runs, real) | 0.051-0.057 (median 0.051) | 0.051-0.064 (median 0.057; noise, nothing in the patches runs at start) | |

The glGetTexImage-with-conversion regression: st maps the tiled texture read-only and converts RGBA -> BGRA from the
map; the old path's CPU detiling leaves the data in a malloc'd buffer that is hot in the cache for the conversion, the
new path's staging map is cache-invalidated memory plus a GPU blit and wait [H]. The driver cannot tell a map that
will be memcpy'd from one that will be converted. Options: accept (glReadPixels, the common screenshot path, is the
big win; glGetTexImage with conversion is wined3d texture downloads and rare), or restrict the read blit to
`PIPE_MAP_READ` maps of >= 1 MiB (the conversion case was measured at 1.6 MB, so that does not help) — there is no
clean split; a st-level change (prefer the blit-to-BGRA-staging path for glGetTexImage like st_ReadPixels does) would
fix both. Everything else in the table is a gain or unchanged within the session's spread.

## 6. Not covered / open

- hasvk (Vulkan) paths: nothing on the device uses them today; not measured this round.
- Shader compile time and the disk cache on the eMMC: only the start-up share (blorp shaders compiled at runtime, ~5 %
  of start user time) was seen; no game-shader compile profile.
- The `getpid` calls per frame and the 6 BO-cache ioctls per batch: identified, not prototyped.
- The st_pbo pass slowness root cause and a crocus blit-from-PBO path (3.6).
- Memory footprint beyond R5/README: unchanged by these patches except transient staging textures.
- lib32 (Wine 32-bit) builds of the patches: same source, not built this round.
- On-screen game runs: only `burst` (SDL) sanity runs, no real game profile (the screen was free but the task asked for
  offscreen work first).
