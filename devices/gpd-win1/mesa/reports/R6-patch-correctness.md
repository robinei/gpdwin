# R6: correctness review of the GPD Mesa patches (1.4 and the 1.5 candidates) and the i915 DKMS patches — 2026-10-12

Scope: the crocus patches installed as `mesa-gpd 1:26.2.4-1.4` (`mesa/0001-*`, `0002-*`), the three 1.5 candidates from
R7 (`mesa/0003-*`, `0004-*`, `0005-*`, identical to `mesa/reports/r7/000[345]-*.patch` and to branch `r7final` of the
scratch Mesa tree), the slim build configuration (`mesa/PKGBUILD`, `mesa/lib32/PKGBUILD`), the driconf trim of 1.5
(`mesa/drirc-trim.py`, `drirc-keep.txt`), and a source-only review of the i915 DKMS patches 0001, 0011-0017.
Everything ran on the GPD from `/tmp/r6` (stock Mesa extracted from `/var/cache/gpd-mesa-stock`, the 1.9 test build
`/tmp/r7/mesa19`, release 1.5 `/tmp/m15` = `mesa/mesa-gpd-1:26.2.4-1.5` byte-identical `libgallium`), under
`flock /tmp/gpd-bench.lock`, offscreen (surfaceless EGL / pbuffer), nothing installed, installed Mesa = 1.4 throughout
(`pacman.log`: last mesa-gpd transaction 01:52, before every run). Legend: **[M]** measured, **[S]** source-verified
(file:line in Mesa 26.2.4 = scratch tree branch `final2`), **[H]** hypothesis.

## 0. Verdicts

| patch | verdict | why (details below) |
|---|---|---|
| 0001 no snooped staging textures (installed) | **ship** (already shipped) | CPU-side coherency of the now non-coherent staging BOs is handled by the existing crocus code paths (WC map for writes, cached map + clflush-invalidate for reads); 12,994 dEQP + 771 piglit + own stress tests identical to stock, 0 data errors |
| 0002 GPU blit for tiled uploads >= 64 KiB (installed) | **ship** (already shipped) | the staging-blit path it enables is the one crocus already uses for busy textures; all formats/targets tested (incl. W-tiled stencil planes, compressed, 3D/array, mips) match stock |
| 0003 threshold 64 -> 256 KiB | **ship** | a constant; uploads of 64-256 KiB return to the stock CPU path |
| 0004 read-only maps of tiled textures via staging blit | **ship** | same staging path as the converting readback (`crocus_map_copy_region`), excludes PERSISTENT/COHERENT/DIRECTLY/ASTC; tested on every readback path incl. depth/stencil planes and compressed; identical results to stock. Known: +1 ms on glGetTexImage with a format conversion (R7), perf only |
| 0005 range-only cache invalidate for read-only buffer maps | **ship** | the invalidated range is exactly the mapped range (`crocus_map_direct` returns `ptr + box.x`), clflush covers partial lines at both ends, falls back to the full invalidate for every other case; stress test with overlapping/unaligned re-reads of GPU-written buffers 0 errors on 1.9 and 1.5 |
| driconf trim (1.5) | **ship**, one nit | drops only `<application>` entries with a plain native executable name; none of the 111 dropped names is installed on the GPD; the file Mesa reads is well-formed (parsed without error, 53 entries). Nit: the ElementTree rewrite drops the `<!DOCTYPE driconf [...]>` and comments; Mesa's expat parser does not use the DTD (`xmlconfig.c` has no DTD/entity handling), confirmed by the 1.5 build loading the file silently |
| slim build config | **ship** | nothing the device uses is missing (section 4) |
| i915 0001 (HS_TX_TIMEOUT u16 overflow) | **ship** | correct; two nits |
| i915 0015 (VLV_IER race) | **ship, consider a small fix** | the fix closes the race except for a residual window of one handler duration right after the restore; a bounded retry loop would close it (section 5) |
| i915 0011-0014, 0016, 0017 (diagnostic/experimental) | keep as is (not upstream material) | no locking/race bug found that can corrupt state; 0012 bypasses the atomic path (by design), runs sleeping work on `system_wq`, and leaves `resync_work` queued across a disable (benign) |

## 1. What the two installed patches change, and why it is correct [S]

### 1.1 The transfer paths in crocus (Mesa 26.2.4, `src/gallium/drivers/crocus/crocus_resource.c`)

`crocus_transfer_map` (`:1529-1680`): `map_would_stall` = resource busy or needs a resolve (`:1563-1572`); `no_gpu` for
PERSISTENT/COHERENT/DIRECTLY (`:1610`), for every map without DISCARD_RANGE that does not need a resolve (`:1620-1622`,
"GPU copies are not useful for buffer reads") and for ASTC (`:1625`). `map_would_stall && !no_gpu` ->
`crocus_map_copy_region` (`:1159-1222`): a linear `PIPE_USAGE_STAGING` texture of the box (`:1170-1180`,
`crocus_resource_configure_main` `:190-192` forces LINEAR for STAGING), for non-DISCARD maps a blorp copy resource ->
staging + `PIPE_CONTROL` RT flush + CS stall (`:1206-1211`), batch flush if the staging is referenced (`:1216`), then
`crocus_bo_map(staging, usage & MAP_FLAGS)`. On unmap, write maps blit staging -> resource (`crocus_flush_staging_region`
`:1122-1148`) and the staging is destroyed (back to the BO cache). Otherwise the resource is mapped directly: W-tiled
stencil through `crocus_map_s8` (CPU swizzle via a `MAP_RAW` map), other tiled surfaces through `crocus_map_tiled_memcpy`
(`:1436`, `MAP_RAW` map + `isl_memcpy`), linear through `crocus_map_direct` (`:1484`).

BO mapping (`crocus_bufmgr.c`): `crocus_bo_map` (`:1061-1093`): tiled BO without RAW -> GTT map; `can_map_cpu`
(`:1026-1058`): coherent BO -> cached map; otherwise cached only for reads on LLC or for reads without
PERSISTENT/COHERENT/ASYNC/RAW; everything else WC. `crocus_bo_map_cpu` (`:853-911`) waits for the BO unless ASYNC and,
for a non-coherent BO on a non-LLC part, `util_flush_inval_range(map, bo->size)` (clflush every line + mfence, with the
Baytrail double-clflush of the last line, `src/util/cache_ops_x86.c:82-115`; `-DSUPPORT_INTEL_INTEGRATED_GPUS` is a
project-wide `pre_args` define whenever crocus is built, `meson.build:1778-1780`). A cached map of a non-coherent BO is
never used for writes (`can_map_cpu` returns false for MAP_WRITE; `crocus_bo_map_cpu` asserts it).

### 1.2 Patch 0001: staging textures no longer `BO_ALLOC_COHERENT`

Before: on non-LLC parts every `PIPE_USAGE_STAGING` texture BO got `I915_GEM_SET_CACHING` cached (snooped) and
`reusable = false` (`crocus_bufmgr.c:438-446`), so it was mapped cached without any clflush, and freshly allocated
(GEM_CREATE + SET_DOMAIN + SET_CACHING) for every upload. After: a normal non-coherent BO from the bucket cache.

What now happens, per direction [S]:
- **Upload staging** (st's own staging for converting uploads, `st_cb_texture.c`; crocus' `crocus_map_copy_region` with
  DISCARD_RANGE): the map is `MAP_WRITE` (+ DISCARD) -> `can_map_cpu` false -> **WC map** (`crocus_bo_map_wc`, waits
  for the BO; the BO cache only hands out idle BOs, `alloc_bo_from_cache:300-330`). CPU writes bypass the cache; the
  GPU reads them after the execbuf (the kernel's `wmb`/ring write orders WC stores). Same mechanism as every vertex/
  index/uniform upload buffer in crocus (those never were coherent: `crocus_resource_create_for_buffer` passes no flags).
- **Readback staging** (st's blit-to-staging for glReadPixels/glGetTexImage with a conversion; `crocus_map_copy_region`
  reads): `MAP_READ` -> `can_map_cpu` true -> **cached (WB) map**, `bo_wait` for the blit, then the clflush-invalidate
  of the whole BO before the CPU reads. Stale lines from a previous use of the same (now reusable) BO, including lines
  prefetched during a previous read, are dropped by that invalidate; nothing dirties those lines (no cached writes).
- **The same physical pages are mapped WB (reads) and WC (writes) at different times** (`map_cpu` and `map_wc` are kept
  per BO). This aliasing is what crocus/old iris always did for non-coherent BOs on non-LLC parts; it is safe on CPUs
  with self-snoop (the GPD's Airmont has `ss` in cpuinfo [M]; Linux' own PAT code trusts self-snoop except on a blacklist
  of Core 2/Nehalem/Sandy Bridge cores). Patch 0001 makes it the standard path for readback staging; see 1.5 for the
  gen4-7 caveat.
- **Kernel side**: the kernel does not clflush on behalf of the WB mmap after the first execbuf (`cache_dirty` is set at
  page allocation and cleared by the clflush at the first execbuf; the `SET_DOMAIN(CPU read)` at allocation flushes
  too), which is why crocus' own invalidate is required and present. The exec path is unchanged by the patch.
- **PBOs, persistent/coherent maps, unsynchronized maps**: buffers are untouched by the patch (`PIPE_BUFFER` goes through
  `crocus_resource_create_for_buffer`); textures cannot be persistently mapped in GL; st never maps textures
  UNSYNCHRONIZED; the threaded context (crocus uses `threaded_context_create`, `crocus_context.c:307`) runs texture
  maps synchronously (`TC_TRANSFER_MAP_THREADED_UNSYNC` only for buffers).
- **MSAA / depth-stencil / Z32F_S8**: `u_transfer_helper` (`crocus_resource.c:1957-1964`: MSAA_MAP, SEPARATE_STENCIL
  and SEPARATE_Z32S8 on gen6+) resolves/splits into its own `PIPE_USAGE_STAGING` resources or per-plane maps; they take
  the same two paths above.
- Not affected: LLC parts (`cache_coherent` is true for every BO there, `bo_alloc_internal:431`), so the removed flag
  was a no-op there; gen4-7 non-LLC parts (gen4/5, Valleyview) get the same change as Cherry Trail (1.5).

### 1.3 Patch 0002: write-only uploads >= 64 KiB into tiled textures via staging + blit

The patch forces `map_would_stall` and thus `crocus_map_copy_region` for write-only maps of tiled textures of
>= 64 KiB (box pixels x blocksize) on non-LLC parts. Observations [S]:
- It only takes effect together with `PIPE_MAP_DISCARD_RANGE` (otherwise `no_gpu` is set at `:1620-1622` and the CPU
  path stays). st/mesa sets `GL_MAP_INVALIDATE_RANGE_BIT` for every (compressed) texsubimage store
  (`texstore.c:969, 1262`) **except** depth-only or stencil-only updates of packed depth-stencil textures, which are
  READ|WRITE (`texstore.c:960-970`) and therefore keep the read-modify-write CPU path. So the patch never discards data
  it must preserve; with DISCARD_RANGE the whole box is undefined before the write and st writes the whole box.
- The path it enables is the one crocus already uses for busy textures with DISCARD_RANGE (upstream behaviour), for
  every target: 2D, array/3D (`templ.array_size = box->depth`, 2D_ARRAY staging, one blorp copy per slice,
  `crocus_blit.c:736-743`), cube faces, mip levels (`dst_level`), compressed formats (blorp copies raw blocks; the
  threshold overestimates compressed uploads by bw x bh, i.e. a DXT1 upload of 512 bytes already counts as 8 KiB —
  perf only), sRGB (raw copy with `res->internal_format`), depth (Y-tiled Z24/Z16/Z32F planes) and **W-tiled stencil**
  (`S8_UINT` plane >= 64 KiB: blorp handles W-tiled destinations with the Y-tiling trick, `blorp_blit.c:2369-2400`).
- Ordering: the staging write lands when the blit executes in the render batch, after all earlier GPU work on the
  texture and before later draws. A later CPU read of the texture sees `resource_is_busy` and waits/flushes as usual.
  Cross-batch (compute) dependencies go through `crocus_use_bo` (`crocus_batch.c:305-350`, flush + sync of the other
  batch), as for every blit.
- DONTBLOCK/DIRECTLY/UNSYNCHRONIZED: DIRECTLY and PERSISTENT/COHERENT are `no_gpu`; DONTBLOCK only matters with DIRECTLY
  (`:1568-1570`, before the patch's line); UNSYNCHRONIZED is never set for textures by st.
- `map->ptr`/stride for the staging box are set by `crocus_map_copy_region` (`:1197-1201`), the caller writes
  box-relative as before.
- Size formula: `res->base.b.format` is the *frobbed* external format (e.g. `Z32_FLOAT_S8X24_UINT`, 8 B) for the depth
  plane of a separate-stencil resource whose internal format is `Z32_FLOAT` (4 B): the threshold is reached at half
  the real size for that one format. Perf only; `res->internal_format` would be exact (same in 0003/0004).

### 1.4 Forcing `map_would_stall`: no semantic change

`map_would_stall` only selects the staging path; it is not used after `:1643`. The flags that make a staging copy
illegal (`no_gpu`) are still honoured, and the "busy" flush/wait of the direct path (`:1662-1667`) is replaced by the
blit's ordering in the batch. Writes become "GPU-visible when the batch runs" instead of "visible immediately", which
is what the busy-texture path already did and what GL allows.

### 1.5 gen4-7 crocus hardware (not testable here)

`has_llc` is false on gen4/5 and Valleyview too, so 0001/0002 (and 0003/0004) change those as well. The staging path
exists upstream for them (busy + DISCARD), and on gen <= 5 `crocus_copy_region` tries the BLT first
(`crocus_blit.c:684-689`) and blorp retiles W-tiled sources for gen < 8 (`blorp_blit.c:2443-2455`). Two caveats that
cannot be verified without hardware: (a) 0002/0004 make the blorp/BLT copy paths the common case for uploads/readbacks
of all formats on gen4/5, which upstream exercises rarely; (b) the WB+clflush readback mapping relies on self-snoop, and
the Core 2 generation paired with gen4/5 GPUs is on Linux' self-snoop erratum list — crocus already uses that mapping for
buffer reads on those parts, so this is not new, but 0001 extends it to readback staging. For an upstream submission:
measure on a gen4/5 or VLV machine, or gate on `devinfo->ver >= 8` (the patches' measurements are all Cherry Trail).

## 2. The 1.5 candidates (R7 0003-0005) [S]

### 2.1 0003: threshold 256 KiB
A constant in the 0002 condition. Uploads of 64-256 KiB return to the stock CPU tiled copy (the path 1.3 used). No
correctness surface.

### 2.2 0004: read-only maps of tiled textures >= 64 KiB through the staging blit
- `read_blit` requires `PIPE_MAP_READ && !WRITE`, no PERSISTENT/COHERENT/DIRECTLY, non-LLC, tiled, >= 64 KiB; it
  removes the `no_gpu` for that case and sets `map_would_stall`; the ASTC `no_gpu` (`:1625`) is still applied before
  the final `if (read_blit && !no_gpu)`.
- Path: `crocus_map_copy_region` without DISCARD -> blorp resource -> linear staging, RT flush + CS stall, batch flush,
  `crocus_bo_map(staging, MAP_READ)` = cached map + wait + invalidate. Unmap: `crocus_flush_staging_region` returns at
  once for a read map (`:1125`), staging destroyed. This is byte for byte the path every converting glReadPixels/
  glGetTexImage already takes (st blits into its own staging and maps it READ), only entered one level lower.
- Coverage of the paths behind it: 2D/array/3D/cube/mips as in 1.3; compressed (raw block copy; glGetCompressedTexImage
  tested); depth planes and **W-tiled stencil planes as blorp *sources*** (gen8 samples W-tiled directly,
  `blorp_blit.c:2443`: the retile is only for gen < 8) — the d24s8 readback test and the piglit depth-stencil tests
  below cover it; MSAA textures never reach it (`u_transfer_helper` resolves into a linear staging first); `PIPE_BUFFER`
  excluded; `crocus_resource_prepare_access` in `crocus_copy_region` handles aux (fast-clear) state like any blit.
- A READ map with UNSYNCHRONIZED (never produced by st for textures) would map the staging WC without a wait and read
  garbage — the same latent hazard exists upstream in the busy-texture path; not reachable.
- Perf regression (R7 section 5): glGetTexImage with a conversion +1 ms (the conversion now reads from freshly
  invalidated memory). Not a correctness issue.

### 2.3 0005: range-only invalidate for read-only buffer maps
- `crocus_map_direct` (`:1484`) calls `crocus_bo_map_range(bo, flags, box->x, box->width)` for `PIPE_BUFFER` READ-only
  maps and returns `ptr + box->x`; the box of a buffer transfer is in bytes, so the invalidated range is exactly the
  bytes the caller may touch (gallium contract; st's `pipe_buffer_map_range`, TC's `tc_buffer_map` and `u_transfer_helper`
  all pass the user's range).
- `util_flush_inval_range` rounds the start down to a cache line and flushes up to `start + size` (`cache_ops_x86.c:42-51`),
  so partial lines at both ends are invalidated; the extra bytes in those lines are clean (no cached writes exist for
  non-coherent BOs), so flushing them cannot write anything back.
- Everything else falls back to `crocus_bo_map` (tiled, WRITE, `!can_map_cpu`, coherent, LLC, out of range) — the whole-BO
  behaviour is unchanged for those.
- What it leaves behind: stale lines outside the mapped range stay in the cache. Every later cached read of a
  non-coherent BO goes through `crocus_bo_map_range`/`crocus_bo_map_cpu`, which invalidates what that read will touch,
  and the hardware prefetcher can only fill lines *after* the invalidate of the current map while the GPU is idle for
  this BO (`bo_wait` precedes the invalidate). So no path reads a line that was not invalidated after the last GPU
  write. The stress test below (overlapping and adjacent re-reads of ranges the GPU rewrote in between, unaligned
  offsets, GPU clears between reads) found 0 errors.
- `can_map_cpu` is forward-declared; `map_cpu` creation duplicates `crocus_bo_map_cpu`'s atomic cmpxchg; `bo_wait`
  unless ASYNC. Fine.

### 2.4 The driconf trim (1.5)
`drirc-trim.py` removes `<application>` entries whose only attributes are `name` and a plain native `executable`
(not `.exe`, not in `drirc-keep.txt` = `firefox`), keeps regexp/sha1/`application_name_match` entries, `<engine>`
entries and the device-wide options. Run on the 26.2.4 file [M]: 164 -> 53 entries; the 111 dropped executables are
Linux games, browsers, desktop compositors and tools (`gnome-shell`, `kwin_*`, `vlc`, `totem`, `chromium`, ...), none
installed on the GPD (`pacman -Q` for the plausible ones: none). The rewritten file (`<?xml version='1.0'
encoding='utf-8'?>` + `<driconf>`; the original's single comment line is lost, it has no DOCTYPE) is parsed by Mesa
without a message and the 1.5 build runs normally with it (`DRIRC_CONFIGDIR=/tmp/m15/usr/share/drirc.d`, section 3).
Mesa's `xmlconfig.c` uses expat with element handlers only (`:1151`), nothing the rewrite changes. Nit: `python3` is
not in `makedepends` explicitly (python-mako etc. pull it in, so the build works).

## 3. Test results [M]

Test environment: dEQP (VK-GL-CTS `opengl-es-cts-3.2.14.1`, modules gles2/gles3/gles31, surfaceless target, pbuffer
256x256, config `rgba8888d24s8ms0` — the default config is 565 and makes ~200 image comparisons fail on every build, so
all runs use the explicit config) and piglit (git `r12436.6e134c17d`, waffle 1.8.2, `-p surfaceless_egl`), both built
on the desktop in the devtools chroot (copies `r6`/`r6b`), run on the GPD from `/tmp/r6`. Own tests in `/tmp/r6/src`
(sources in this session's scratchpad; `cohtest.c`, `bufread.c`, `vkcheck.c`, `dstest.c`): byte-exact round trips with
random data and random sizes, BO-cache churn and "poisoning" with other data between runs.

Builds: **stock** = Arch `mesa 1:26.2.4-1`; **1.4** = installed `mesa-gpd` (0001+0002); **1.9** = R7 test build
(0001-0005, `/tmp/r7/mesa19`); **1.5** = release build (0001-0005 + driconf trim, `/tmp/m15`).

### 3.1 dEQP (12,994 cases per build: GLES2 functional texture/read_pixels/fbo/buffer; GLES3 texture specification/
format/compressed/swizzle/shadow/wrap/mipmap/units/size, read_pixels/fbo/buffer/pbo; GLES31 copy_image subsets
(rgba8/r8/rgb8/rg8 view classes, mixed 128-bit, astc 8x8, etc2), texture gather/border_clamp/specification/multisample)

| build | Pass | NotSupported | Fail | failures |
|---|---|---|---|---|
| stock | 12798 | 188 | 8 | 8x `GLES31.functional.copy_image.compressed.viewclass_etc2_*` (texture2d_to_texture2d) |
| 1.4 | 12798 | 188 | 8 | the same 8 |
| 1.9 | 12798 | 188 | 8 | the same 8 |
| 1.5 | 12798 | 188 | 8 | the same 8 |

Per-chunk failure sets compared mechanically: identical across all four builds. The 8 etc2 failures are pre-existing (stock).

### 3.2 piglit (1,042 tests selected by name: texsubimage/teximage/getteximage/readpixels/pbo/copyteximage/
arb_copy_image/arb_get_texture_sub_image/arb_map_buffer_range/arb_buffer_storage/arb_texture_storage/s3tc/rgtc/bptc/
depthstencil/fbo-depth*/fbo-readpixels/ext_texture_array/texture formats/texwrap/arb_clear_texture/... ; multisample
accuracy, GLX and EGL tests excluded)

| build | pass | skip | fail | crash | timeout | incomplete |
|---|---|---|---|---|---|---|
| stock | 771 | 57 | 29 | 168 | 1 | 16 |
| 1.4 | 771 | 57 | 29 | 167 | 2 | 16 |
| 1.9 | 771 | 57 | 29 | 167 | 2 | 16 |
| 1.5 | 771 | 57 | 29 | 167 | 2 | 16 |

Only one test differs: `arb_texture_buffer_object@formats (vs, arb)` crash (stock) vs timeout at 45 s (1.4, 1.9, 1.5): a slow
test at the timeout boundary (it also fails to finish in 60 s on every build when run by hand), not a driver difference. The non-pass classes are test-environment limits, identical on
every build: the 167/168 crashes are SIGABRT (rc -6) of tests that need a *window* visual with depth/stencil or
multisamples (`fbo-depthstencil ... default_fb`, `depthstencil-render-miplevels`, `copyteximage` with samples) — no
window on the surfaceless platform; the 29 fails are 26 missing `.shader_test` files (not extracted to /tmp) + 2
`ext_texture_array` render tests + `bptc-compress-alpha` (same values on stock); the 16 incompletes are the ASTC
tests (data files not extracted). The 168 crashed tests were re-run by hand with `-fbo` on stock / 1.4 / 1.5: 75 pass, 92 skip (the `samples=N` and
`default_fb` variants refuse `-fbo`), 1 does not finish (the slow `formats` test above) — identical lists on all three
builds. The 75 that run with `-fbo` include all 45 `depthstencil-render-miplevels 585 <d/s/ds=z24_s8...>` variants of
`ext_packed_depth_stencil` and `arb_depth_buffer_float`, `fbo-blit-d24s8`, `copyteximage {1d,2d}_array` and the
`fbo-depthstencil` default_fb set without samples.

### 3.3 Own stress tests (all byte-exact compares; sizes include both sides of the 64/256 KiB thresholds)

`cohtest` (2D RGBA8 upload via RGBA = direct map and via BGRA = st staging, partial boxes, glGetTexImage RGBA/BGRA,
draw + glReadPixels RGBA/BGRA, pack PBO readback, unpack PBO upload, 2D array and 3D partial-layer updates, 4 mip
levels, DXT5 (512x512, partial block updates, glGetCompressedTexImage), DEPTH24_STENCIL8 upload/readback incl.
GL_STENCIL_INDEX readback and FBO glReadPixels DEPTH_STENCIL, sRGB8_A8/RG8/R8/RGBA16F/Z24/Z16/Z32F round trips,
glCopyImageSubData, busy-texture upload ordering, then 60 random sizes up to 1024x768 with BO-cache churn):

| build | result |
|---|---|
| stock | PASS 870 checks |
| 1.4 | PASS 870 checks |
| 1.9 | PASS 870 checks |
| 1.5 | PASS 632 checks (30 random iterations; 64-bit) |

`bufread` (4 MiB buffer rewritten by the GPU each round via glCopyBufferSubData, then 6 reads of random unaligned
ranges that overlap/straddle the previous read's cache lines through glMapBufferRange(READ) and glGetBufferSubData,
a partial GPU update followed by a read around it, transform-feedback output read through a sub-range map, then a
GPU clear and the same range read again; 120 rounds = 900 compares): PASS on stock, 1.4, 1.9 (0 errors).

32-bit (lib32, what Wine uses; `cc -m32`, 30/60 rounds): `cohtest32` PASS 630 checks and `bufread32` PASS 450 checks on
each of stock `lib32-mesa`, installed `lib32-mesa-gpd` 1.4 and 1.5 (`/tmp/m15/usr/lib32`).

`vkcheck` (hasvk: host-visible buffer -> device buffer -> optimal-tiled image -> host buffer, 1024x512 RGBA8, 10
iterations): PASS on the installed 1.4, stock and 1.5 hasvk; `api 1.2.354`.

`dstest`: a depth-only `glTexSubImage2D(GL_DEPTH_COMPONENT)` into a DEPTH24_STENCIL8 texture corrupts the stencil plane
and `GL_STENCIL_INDEX` texsubimage is rejected with INVALID_OPERATION — **identical on stock**, a pre-existing st/mesa
behaviour (`u_transfer_helper` read-modify-write of separate planes), not caused by our patches. Noted, not pursued.

## 4. The slim build: anything missing?

File lists of the stock packages vs ours [M] (`bsdtar -tf`): `mesa-gpd` lacks only the other drivers' `dri/*_dri.so`,
their `drirc.d/00-*-defaults.conf` and `libgallium` carries our version in its name; `lib32-mesa-gpd` the same
(`crocus_dri.so`, `kms_swrast_dri.so`, `libdril_dri.so`, `swrast_dri.so`, `libEGL_mesa`, `libGLX_mesa`,
`libGLX_indirect`, `libgbm`, `dri_gbm.so`, pkgconfig all present). Consumers on the device: Xwayland/GLX
(`libGLX_mesa` + `libdril_dri`), wlroots GBM/EGL (`libgbm`, `dri_gbm.so`, `libEGL_mesa`), Qt6/gstreamer/RetroArch/SDL
(EGL/GL/GLES; `gles1=disabled` as in Arch), Wine 32-bit (lib32 set complete), Vulkan loader + `vulkan-intel-gpd`
(hasvk; anv does not support gen8), implicit layers package present. VA-API: stock Mesa has no VA driver for crocus
either (`gallium-va` drivers are radeonsi/nouveau/virgl only), so `-D gallium-va=disabled` loses nothing;
`libva` on the device has no Intel backend installed in either case. OpenCL (`opencl-mesa`/rusticl) and
`mesa-vdpau` are not installed. LLVM/SPIRV-Tools are build-time only for crocus/hasvk (`mesa_clc` two-stage build);
softpipe (no llvmpipe) is the software fallback and works (`LIBGL_ALWAYS_SOFTWARE` checked in README 1.3).
`llvm-libs`/`spirv-tools` packages are still installed on the device for other reasons, so the stock Mesa in
`/var/cache/gpd-mesa-stock` still loads for rollback.

## 5. i915 DKMS patches (source review only, no builds)

- **0001** (`txbyteclkhs` u16 -> u32, 64-bit multiply): correct; the non-burst `MIPI_HS_TX_TIMEOUT` value for this panel
  (0xb95ff) fits the 24-bit field (`HIGH_SPEED_TX_TIMEOUT_COUNTER_MASK 0xffffff`). Nits: the write is not masked with
  that mask (a > 16.7M-byte-clock frame would spill into reserved bits), and `pixels_from_txbyteclkhs` still multiplies
  in 32 bits (`clk_hs * lane_count * 8 * 100`, fine for the per-segment values it is used for).
- **0015** (VLV_IER lost to a racing handler): the analysis of the race is right (`i915_irq.c:263/279, 336/349` clear
  VLV_IER with `rmw` and restore the read value, without `display->irq.lock`). The fix (synchronize_irq, then rewrite
  the mask under the lock) is called from the power-well enable/disable (`intel_display_power_well.c:1312/1337`),
  process context, no spinlock held, so `synchronize_irq` may sleep there. Residual window: a handler H2 that *starts*
  after the racing handler H1 wrote 0 and before the restore write reads 0 and writes it back after the restore. Window =
  the time between H1's restore and ours (microseconds, once per power-well cycle), so the drm_info would show it.
  Suggested fix: loop `write mask; synchronize_irq; read` until the read equals the mask (bounded, 3 tries): after a
  synchronize every handler that started after the write has read the mask. Same for the disable side.
- **0011** (poller logging underruns): fine; the 5 ms self-rescheduling delayed work is cancelled with
  `cancel_delayed_work_sync` (handles self-requeue).
- **0012** (classification, vblank FIFO sampler, resync): no race that corrupts state found. Notes: (a) the `ur` fields are
  shared between the delayed work (system_wq), the vblank kthread work and the resync work without a lock — all
  single-writer per field or flag handshakes (`fifo_pending`/`fifo_new`), benign on x86 TSO, a missed `fifo_new` only
  delays one evaluation; (b) the resync deliberately bypasses the atomic path (direct `TRANSCONF` toggle, planes via
  `intel_plane_disable_arm`/`update_arm` with `NULL` dsb, no watermark/cxsr update, no `assert_*`), correct for the
  purpose, not upstreamable; it holds the crtc and plane modeset locks and waits for the previous commit's
  `hw_done`/`flip_done`, so it cannot overlap a commit; `drm_vblank_work_flush_all` + `intel_crtc_vblank_off/on` with
  `intel_crtc_update_active_timings` mirrors the modeset path; (c) `vlv_dsi_resync_pipe` sleeps up to ~0.5 s
  (`poll_timeout_us`, `usleep_range`) on `system_wq` — acceptable, but `system_unbound_wq` or an own workqueue would be
  the clean choice; (d) `vlv_dsi_ur_disable` does not cancel `resync_work` and `vlv_dsi_ur_enable` does not reset
  `resync_pending`: a resync requested just before a modeset runs after it (sees the crtc active again) and performs one
  spurious resync (one black frame) — benign; (e) the 2026-10-11 D-state hang (vblank work queued forever) is the
  0016 fix; (f) `vlv_dsi_fifo_work` runs `udelay(5)` x 12 on the vblank kthread: fine.
- **0013** (start the poller on fastset): `update_pipe` runs in the commit tail; `vlv_dsi_ur_enable` only initialises and
  schedules; guarded by `!ur->enabled`. Fine.
- **0016** (`drm_vblank_work_cancel_sync` after 200 ms): correct; this is the right call to unblock
  `drm_vblank_work_flush_all`.
- **0017** (deferred resync): the deferred request is only re-issued when no underrun is being classified and no resync
  is pending/running, cleared on PAIR/stall/enable. Fine.

## 6. Not covered

- gen4-7 crocus hardware (section 1.5): source review only.
- dEQP: GLES2/3/31 subsets only (texture, read_pixels, fbo, buffer, copy_image, pbo); no desktop GL CTS (`KHR-GL*`),
  no Vulkan CTS (hasvk: one own sanity test).
- piglit: 92 window-visual/multisample-window tests cannot run on the surfaceless platform at all (section 3.2);
  shader_test-based and ASTC tests not extracted; no `-j` concurrency.
- On-screen/windowed runs (wlroots dmabuf/shm paths, Xwayland) were not part of this review; the 1.4 README and R7 have
  the functional checks for those.
- Patch 0004 perf regression on converting glGetTexImage (R7) is acknowledged, not re-measured here.
- The i915 patches: no module built or loaded; 0002-0010, 0018, 0019 (experiments not in the DKMS set) not reviewed.
