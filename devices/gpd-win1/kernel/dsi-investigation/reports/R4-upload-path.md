# R4: which texture-upload path triggers the DSI pipe pauses, and why (2026-10-11)

Scope: the question from FINDINGS.md "Reproducer" — why SDL's `opengl` renderer (and sway's shm uploads)
trigger the DPI FIFO underruns while `opengles2`/`vulkan`/`gpu` do not. Sources read: Mesa 26.2.4, SDL
3.4.18, wlroots 0.20.2 (device: `wlroots0.20 0.20.2-1`, `sway 1.12-4`, `mesa 26.2.4-1`), Linux 7.2.9 i915
(`kernel/i915/build/src`). Experiments: 2-minute runs on the device, counted with
`journalctl -k | grep classified | grep -v echo` (test module 0018 still loaded; the ftrace capture service
had died at 20:51, so no per-event timing this round, counts only). Test programs: `r4gl.c`, `r4drm.c`,
`r4egl.c` (device `/tmp/r4/`, desktop scratch dir `r4/work/`; not in the repo — copy them next to
`burst.c` if they are to be kept).

## 1. Correction first: the GL driver on this device is crocus, not iris

Mesa's loader maps PCI 8086:22b0 (CHV, `lspci`) to **crocus**: `src/loader/pci_id_driver_map.h:59`
(`crocus_chip_ids`, matched before the iris predicate) with `include/pci_ids/crocus_pci_ids.h:101`
(`CHIPSET(0x22b0, chv, ...)`). Confirmed on the device: `INTEL_DEBUG=bat` output is printed by
`crocus_fence.c:220` ("render batch [1] flush with ... 12 command relocs, 7 state relocs"; iris never
relocates). The Vulkan driver is hasvk (correct in FINDINGS). Everything below is crocus; the iris
reasoning in FINDINGS ("SYSTEM_MEMORY_UNCACHED", `iris_texture_subdata`) does not apply to the GL path,
and its hypothesis (CPU WC tiled copy = trigger) is refuted below: it is the other way round.

## 2. Code paths (source-verified)

### 2a. SDL `opengl` renderer → GPU blit from a fresh snooped staging buffer (the unsafe path)

- SDL: `src/render/opengl/SDL_render_gl.c:420-433` `convert_format()`: `SDL_PIXELFORMAT_XRGB8888`
  (= `BGRX32`) → internal format **`GL_RGBA8`** (sized), format `GL_BGRA`, type `GL_UNSIGNED_BYTE`.
  `GL_CreateTexture` does `glTexImage2D(..., NULL)` (685); `GL_UpdateTexture` (792-807) does
  `glPixelStorei(GL_UNPACK_ROW_LENGTH)` + `glTexSubImage2D(GL_BGRA, GL_UNSIGNED_BYTE)` every frame.
- Mesa texture format choice: `st_ChooseTextureFormat` (`st_format.c:1343`) → `choose_format_with_bindings`
  → `st_choose_format` (1189): the "pick the format that matches format/type so we can memcpy" branch only
  runs for **unsized** internal formats (`_mesa_is_enum_format_unsized`); `GL_RGBA8` is sized, so the
  table wins: `{ 4, GL_RGBA, GL_RGBA8 }` (264) → `DEFAULT_RGBA_FORMATS` (197-201) → first supported =
  **`PIPE_FORMAT_R8G8B8A8_UNORM`**. The texture is RGBA, the upload is BGRA.
- `st_TexSubImage` (`st_cb_texture.c:2142`): the `texture_subdata` fast path (2183-2217) needs
  `_mesa_texstore_can_use_memcpy` (`main/texstore.c:864`), i.e.
  `_mesa_format_matches_format_and_type(R8G8B8A8_UNORM, GL_BGRA, GL_UNSIGNED_BYTE)`, which is false.
  crocus sets `caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT` (`crocus_screen.c:364`) so
  `prefer_blit_based_texture_transfer` is true and the **blit path** (2290-2412) is taken: create a
  `PIPE_USAGE_STAGING` texture in `src_format = B8G8R8A8_UNORM` (`st_choose_matching_format`), map it
  `PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED`, memcpy the rows (CPU), unmap, `pipe->blit()` staging →
  texture (channel swizzle, `PIPE_TEX_FILTER_NEAREST`), drop the staging resource.
- crocus: the staging resource is linear (`crocus_resource.c:190-192`) and allocated with
  `BO_ALLOC_COHERENT` (665-666) → `crocus_bufmgr.c:438-446`: on a non-LLC part (`bo->cache_coherent =
  has_llc` = false, 431) it issues **`DRM_IOCTL_I915_GEM_SET_CACHING(CACHED)`** (= snooped) and marks the
  BO **`reusable = false`** (445): every staging buffer is a brand-new GEM object, never from the BO
  cache. Its CPU map is a WB (cached) `MMAP_OFFSET` map (`can_map_cpu` 1024-1030 → `crocus_bo_map_cpu`
  853). The blit is `crocus_blit` (`crocus_blit.c:388`) → `blorp_blit` (610): a 3D-pipeline draw on the
  **render engine** that samples the linear snooped surface (MOCS = `isl_mocs(dev, 0, external=false)`,
  `crocus_resource.h:300-303`) and writes the Y-tiled `R8G8B8A8` texture.
- Measured per frame (strace of `burst -r opengl` and of `r4gl` variant A, 10 frames):
  `GEM_CREATE, SET_DOMAIN(CPU), SET_TILING, SET_CACHING, MMAP_OFFSET, EXECBUFFER2, GEM_CLOSE` once each
  plus `GEM_BUSY`/`MADVISE`; the `SET_DOMAIN` call takes ~7 ms (page allocation + clflush, section 3).
  `INTEL_DEBUG=bat` per frame: PIPE_CONTROL(depth/DC/RT flush + CS stall), PIPE_CONTROL(state/constant/
  texture/instruction cache invalidate + CS stall), blorp blit draw, blorp clear draw, two stalling
  PIPE_CONTROLs and a final RT/DC flush, `MI_BATCH_BUFFER_END` (`/tmp/r4/bat-A.txt`).

### 2b. SDL `opengles2` renderer → CPU tiled copy through a WC map, no GPU work (the safe path)

- SDL: `src/render/opengles2/SDL_render_gles2.c:1737-1743`: every 32-bit format, including BGRX32,
  uses **`GL_RGBA` / `GL_UNSIGNED_BYTE`** with internal format `GL_RGBA` (unsized, 1922); the BGRX byte
  order is corrected in the fragment shader (1189-1245, `GLES2_IMAGESOURCE_TEXTURE_ARGB/BGR`). No CPU
  conversion. `GLES2_TexSubImage2D` (1957-1988) uploads with one `glTexSubImage2D`.
- Mesa: in a GLES context `choose_format_with_bindings` (`st_format.c`, `_mesa_is_gles` branch) calls
  `st_choose_matching_format(GL_RGBA, GL_UNSIGNED_BYTE)` → `R8G8B8A8_UNORM`; the upload matches, so
  `st_TexSubImage` takes `pipe->texture_subdata` (2183-2217). crocus has no own hook
  (`crocus_resource.c:1964`: `u_default_texture_subdata`) → `crocus_transfer_map` (1531) with
  `PIPE_MAP_WRITE | PIPE_MAP_DISCARD_RANGE` (`u_transfer.c:74-95`): the texture is not busy (the
  previous frame's tiny batch has retired), so no staging; `crocus_map_tiled_memcpy` (1433) →
  `crocus_unmap_tiled_memcpy`: `crocus_bo_map(res->bo, MAP_WRITE | MAP_RAW)` = a **WC mmap of the
  texture itself** (`crocus_bo_map` 1061-1070: tiled BOs would get the GTT map, `MAP_RAW` avoids it;
  `can_map_cpu` false → `crocus_bo_map_wc` 913) and `isl_memcpy_linear_to_tiled`. Nothing is submitted
  to the GPU for the upload; strace per frame: only `GEM_BUSY`, `MADVISE`, `EXECBUFFER2`.

### 2c. wlroots (sway) GLES2 renderer for wl_shm client buffers → either path, depending on timing

- `render/gles2/pixel_format.c:18-21`: `DRM_FORMAT_XRGB8888` → `GL_BGRA_EXT` / `GL_UNSIGNED_BYTE`,
  internal format = `GL_BGRA_EXT` (`texture.c:329-333`); texture created with `glTexImage2D(..., data)`
  (344-347) and updated per damage rect with `glTexSubImage2D` + `GL_UNPACK_ROW_LENGTH/SKIP_*_EXT`
  (`texture.c:78-93`, `gles2_texture_update_from_buffer`, called at client commit time). sway's renderer
  is the wlroots default order gles2 → vulkan → pixman (`render/wlr_renderer.c:222-269`); no
  `WLR_RENDERER` override found (sway's own environment could not be read: capability-protected
  process). The compositor also runs crocus.
- Mesa GLES: `GL_BGRA` is treated as `GL_RGBA` and matched → `B8G8R8A8_UNORM`; `GL_BGRA/UNSIGNED_BYTE`
  matches, so the memcpy-capable `texture_subdata` path is used **if the texture is not busy**
  (`crocus_transfer_map`: `map_would_stall = resource_is_busy()` = `GEM_BUSY` or referenced by the
  unflushed batch, 1029-1038). sway samples every client texture when it composes a frame, so a commit
  that arrives while the previous composition still runs on the GPU (or before its batch was flushed) hits
  `crocus_map_copy_region` (1156): a fresh `PIPE_USAGE_STAGING` (snooped, linear) resource,
  `crocus_bo_map` WB, memcpy, and at unmap `crocus_flush_staging_region` → `crocus_copy_region`
  (`crocus_blit.c:668`, blorp copy on the render engine) — the same structure as 2a. An idle texture takes
  the CPU tiled-copy path of 2b. Which one sway hits per frame was not measured (no strace of sway); only
  the busy path is implicated by the experiments.

### 2d. The other SDL renderers

`vulkan` (hasvk) and `gpu` (SDL GPU on Vulkan) use SDL's own staging buffer + `vkCmdCopyBufferToImage`;
`software` goes through 2c. Not analysed further.

## 3. What the kernel does with a crocus staging BO (7.2.9, `gem/`)

Per frame of the unsafe path, in the order strace shows:
1. `GEM_CREATE` (1.6 MB shmem object, cache level NONE: `i915_gem_object_set_cache_coherency`
   (`i915_gem_object.c`) gives `cache_coherent = 0`, `cache_dirty = true` on non-LLC).
2. `SET_DOMAIN(CPU)` (`i915_gem_domain.c:493`): pins the pages = allocates 400 shmem pages, and
   `__i915_gem_object_set_pages` (`i915_gem_pages.c:37-42`) **clflushes the whole object** because
   `cache_dirty`; `set_to_cpu_domain` clflushes again (507-510). This is the ~7 ms step; it runs during
   `SDL_UpdateTexture`, 5-10 ms before the batch (so not the thing 0.5 ms before the underruns).
3. `SET_TILING(NONE)`, `SET_CACHING(CACHED)` (`i915_gem_set_caching_ioctl` 335 → `set_cache_level` 266:
   snooped, `cache_coherent = READ|WRITE`, `cache_dirty = true`). On CHV snooping is a PTE attribute:
   PAT entries 0 and 4-7 carry `CHV_PPAT_SNOOP` (`gt/intel_gtt.c:632-665`), so the GPU's reads of this BO
   snoop the CPU caches, where the memcpy'd data sits dirty (WB map).
4. `MMAP_OFFSET` + `mmap` (WB), memcpy 1.6 MB.
5. `EXECBUFFER2`: bind into the PPGTT (400 PTEs with the snoop PAT), no clflush (`eb_move_to_gpu`,
   `i915_gem_execbuffer.c:2117`: `cache_dirty & ~cache_coherent` = 0), request on the render engine (the
   `i915_request_add` that the traces showed 0.25-0.6 ms before every underrun).
6. `GEM_CLOSE` right after: deferred free → unbind when the request completes → PTE clear, pages freed, and
   a **full GT TLB invalidation** (`i915_gem_pages.c:198-238` → `gt/intel_tlb.c:121`
   `intel_gt_invalidate_tlb_full`: forcewake + per-engine TLB invalidate registers with waits).

The safe path (2b) does none of this; the texture BO lives for the whole run and the GPU never touches
the upload.

## 4. Experiments (device, 120 s each, 853x480, vsync, 10 ms sleep before each frame)

Reference from FINDINGS: `burst -m upload` opengl ≈ 6.6 events/min. Counting window = program start to
exit. The first series was started from a user unit whose environment carries `DISPLAY=:0`, so SDL used
**Xwayland/GLX** there ("X11": identical client-side Mesa path, presentation through Xwayland); the later
runs forced `SDL_VIDEO_DRIVER=wayland` ("WL"). Rates differ between the two, zero/non-zero does not.

| run | program / path | GPU reads the upload? | source BO | events |
|---|---|---|---|---|
| A | `r4gl -a gl -i rgba8 -f bgra -m sub` = SDL opengl replica: st staging + blorp blit (2a) | yes, render engine (sampler) | fresh snooped linear BO per frame (Mesa-owned) | **19** (X11), **7** (WL, run Aw) |
| B | `r4gl -a gl -i rgba8 -f rgba -m sub` = matching format → CPU tiled WC copy (2b) | no | — | 0 (X11), 0 (WL, Bw) |
| C | `r4gl -a es2 -i rgba -f rgba -m sub` = SDL opengles2 replica (2b) | no | — | 0 |
| D | `r4gl -a es2 -i bgra -f bgra -m sub` = wlroots format, texture idle (2c idle case) | no | — | 0 |
| EB | `-a es2 -i bgra -f bgra -m sub -B` = texture busy at upload: staging + blorp copy, + draw (2c busy case) | yes (copy, then draw) | fresh snooped (Mesa) | 2 (115 s) |
| HB | `-a gl -i rgba8 -f rgba -m sub -B` = same in GL | yes | fresh snooped (Mesa) | 1 |
| AD | A + draw the texture after the upload in the same batch | yes (blit, then draw) | fresh snooped (Mesa) | 5 |
| F | `-m pbo`: `glBufferData(STREAM)` PBO refilled each frame, `glTexSubImage2D` from it (st_pbo upload draw) | yes, render engine | persistent uncached (WC), BO-cache reused | 0 |
| F2 | `-m pbo2`: `glBufferStorage` persistent/coherent PBO (crocus does *not* snoop it: no `SET_CACHING` seen) | yes | persistent uncached | 0 |
| G | `-m copy`: `glCopyTexSubImage2D` from an FBO texture (GPU→GPU blorp blit, no CPU data) | yes | persistent tiled uncached | 0 |
| N | `-m none`: clear + swap only | — | — | 0 |
| SW | `burst -r software` (SDL software renderer → compositor upload) | compositor side | | 4 (X11; FINDINGS: 73/5 min on WL) |
| R1 | `r4drm -S -W -R`: persistent snooped BO, CPU memcpy, **blitter-engine** copy, headless | yes, BCS | persistent snooped | 0 |
| R2 | `r4drm -S -F -W -R`: fresh snooped BO per frame (replica of the kernel sequence in 3), BCS copy | yes, BCS | fresh snooped | 0 |
| R3 | `r4drm -F -W -R`: fresh uncached BO per frame, WC write, BCS copy | yes, BCS | fresh uncached | 0 |
| X1 | R2 running together with an on-screen `r4gl -m none` client at 60 fps (sway composes/flips every frame) | yes, BCS | fresh snooped | 0 |
| ES | `r4egl -S`: own persistent **snooped** linear BO imported as dma-buf EGLImage, CPU memcpy, `glCopyTexSubImage2D` (blorp blit on the render engine, external MOCS) | yes, render engine | persistent snooped (imported) | 0 (WL) |
| ESF | `r4egl -S -F`: same with a fresh snooped BO per frame (create, set_domain, set_caching, mmap, memcpy, import, blit, close) | yes, render engine | fresh snooped (imported) | 0 (WL) |
| EU | `r4egl`: persistent uncached (WC) imported BO, same blit | yes, render engine | persistent uncached | 0 (WL) |

Notes: the first D run ended after 3 frames (a key event; the loop ignores key events since) — the listed
D is the rerun. R1-R3 were headless (no client on screen, no flips) and therefore not comparable with A;
X1 repeats R2 with display activity. ESF initially leaked (an FBO per frame kept the textures alive →
OOM-kill at 3.2 GB); the listed ESF is the fixed rerun with flat memory.

## 5. Interpretation

Established:
- The **CPU tiled copy through the WC mapping does not trigger** the pauses (B, Bw, C, D: 0 in 8 min
  interleaved with A/Aw giving 26 in 4 min). The FINDINGS hypothesis (CPU WC write path = trigger) is
  refuted; the trigger is the **GPU-side staging path** that `glTexSubImage2D` takes when the upload
  format does not match the texture format: SDL `opengl` always (GL_RGBA8 vs GL_BGRA), sway's shm
  uploads whenever the client texture is still busy. SDL `opengles2` and an idle wlroots texture use the
  CPU path. This explains the renderer results in FINDINGS and the `full`-vs-`upload` difference partly
  (see below).
- The trigger is not "a GPU read of CPU-written memory" (F, F2: 0), not "a 1.6 MB blorp blit" (G, EU: 0),
  not the per-frame GEM create/clflush/snoop/close/TLB-invalidate churn (R2, X1, ESF: 0), and not a
  snooped source read by the blitter engine (R1, R2, X1: 0) **or by the render engine through an
  imported dma-buf** (ES, ESF: 0).
- The rate drops a lot when the same batch also samples the uploaded texture (AD 5, EB 2, HB 1 vs A 19;
  FINDINGS `full` 1-7/10 min vs `upload` 66). Rates also vary between sessions and X11/Wayland (A 19 vs
  Aw 7; SW 4 vs 73/5 min), so only zero vs clearly non-zero is read from these runs.

What distinguishes A/Aw (and sway's busy path) from the replicas that stayed at zero — the remaining
candidates for the actual ingredient:
1. **MOCS of the source surface.** Mesa-owned BOs get the internal MOCS, imported dma-bufs the external
   one (`crocus_mocs()` → `isl_mocs(dev, 0, external)`, `crocus_bufmgr.c:1287` sets `bo->external` on
   import). On gen8 (`isl.c:206-218`) internal = **0x78 "MemoryType WB, L3, defer to PAT"**, external =
   **0x18 "UC with fence if coherent cycle, L3, defer to PAT"** — the external value is precisely the
   setting meant for coherent (snooped) cycles. ES/ESF therefore read snooped memory with the
   coherent-cycle-aware MOCS and were silent; A reads snooped memory with a WB MOCS, the one combination
   no replica produced. On CHV the PTE snoop bit makes the transaction snoop (`intel_gtt.c:637-641`), the
   MOCS still selects the GT-side cache policy/transaction type. (Compare BXT: a WB MOCS entry implied
   snooping with a large cost, `drm/i915/bxt: Fix inadvertent CPU snooping due to incorrect MOCS
   config`.) This is the strongest remaining candidate.
2. The exact st blit (format-converting `blorp_blit` with `PIPE_MAP_UNSYNCHRONIZED` staging map) vs the
   `glCopyTexSubImage2D` blit of ES: both are blorp sampler copies with the same swizzle; unlikely but not
   excluded.
3. Something in the kernel's handling of a Mesa-owned snooped BO that an imported one does not get
   (both are the same GEM object type; `SET_TILING(NONE)` is the only ioctl ESF did not replicate).

Mechanism (hypothesis, consistent with everything measured, not proven at the hardware level): the DSI
pipe pauses while the render engine streams a cacheable (internal-MOCS) read of snooped memory. On CHV
such reads must be serviced through the CPU-side coherence path (the data is dirty in the CPU L2 after
the memcpy through the WB map), and a 1.6 MB burst of them seems to block the memory path for long
enough that the DPI FIFO (1/3 line) runs dry without the plane FIFO (much deeper) underrunning. The
FINDINGS timing (underrun 0.25-0.6 ms after `i915_request_add`) matches the blit being the first thing
the batch does. Why an additional draw in the same batch lowers the rate is not understood (it adds a
texture-cache invalidate between the two draws and lengthens the batch, which may move the blit relative
to the vulnerable scanline window — FINDINGS "frame position").

## 6. Candidate fixes, ranked

1. **Mesa crocus: do not snoop staging buffers on CHV** (`crocus_resource.c:665-666`,
   `crocus_bufmgr.c:438-446`): allocate `PIPE_USAGE_STAGING` resources without `BO_ALLOC_COHERENT` on
   non-LLC (WC map via `crocus_bo_map_wc`, BO stays reusable). Expected: the blit reads WC memory like
   F/F2/G/EU (all 0) and the per-frame GEM churn disappears; covers SDL opengl, sway's busy-texture
   uploads and every other crocus staging user at once. Risk: staging *reads* (glReadPixels,
   `glGetTexImage` via blit) become uncached CPU reads — slow, but that is what iris accepts on non-LLC
   for WC maps; correctness is unaffected (WC writes are flushed by the kernel's `set_domain`/execbuf
   path as for any uncached BO). Test: build crocus with the change, run `r4gl -a gl -i rgba8 -f bgra`
   (A) and `burst -r opengl` for 5 min via `LIBGL_DRIVERS_PATH`/`LD_LIBRARY_PATH`; expect 0 (vs 7-19 per
   2 min). If instead the MOCS is the ingredient (section 5, candidate 1), a cheaper alternative is to use
   the external (uncached) MOCS for coherent BOs on CHV in `crocus_mocs()` (`crocus_resource.h:300`:
   `external || bo->cache_coherent`), testable the same way — this is the run that discriminates
   candidate 1 from 2/3.
2. **Mesa st: avoid the blit path for a pure channel swizzle** — in `st_choose_format` extend the
   "matching format" branch to sized 8-bit-per-channel internal formats (`GL_RGBA8` with
   `GL_BGRA/UNSIGNED_BYTE` → `B8G8R8A8_UNORM`). Then SDL opengl takes the CPU path (B = 0). Narrower (sway's
   busy case and other mismatched uploads keep staging) and changes format selection for every driver;
   test: `r4gl -a gl -i rgba8 -f bgra` → 0.
3. **Kernel i915: refuse `I915_CACHING_CACHED` on CHV** (`i915_gem_set_caching_ioctl`:
   `!HAS_LLC && !HAS_SNOOP` → -ENODEV) so no userspace can create snooped BOs; crocus handles the failure
   (`cache_coherent` stays false, BO reusable, WC map — it is the LLC-less fallback it already has). Same
   effect as 1 for all drivers at once (iris also snoops staging on non-LLC: `iris_bufmgr.c:912-913`,
   `1321-1323`); risk: hasvk host-coherent memory and media drivers may rely on snooping on CHV — check
   `anv`/hasvk's `I915_GEM_SET_CACHING` use before trying. Alternatively a kernel-side display tweak
   (watermarks/FIFO for the snoop case) is not supported by the data (plane FIFO never underruns).
4. **wlroots/sway**: no code change can avoid staging for a busy texture except uploading later; the
   practical mitigation is to avoid shm uploads: dmabuf-capable clients (SDL `opengles2`/`vulkan`/`gpu`
   renderers hand sway dmabufs; no compositor upload at all). `WLR_RENDERER=vulkan` would move sway's shm
   uploads to hasvk (`vkCmdCopyBufferToImage` from a host-visible buffer) — a 5-minute `burst -r
   software` test tells whether that path is safe.
5. **Per-application**: `SDL_RENDER_DRIVER=opengles2` for SDL games (verified in FINDINGS: Hellfire
   0 underruns), i.e. use a format that matches so the upload stays on the CPU.

## 7. Open points

- The ingredient that makes Mesa's own staging blit different from the imported-BO replica (section 5,
  candidates 1-3) is the one thing left to pin down; the crocus MOCS experiment in fix 1 (or fix 1
  itself) decides it. No Mesa build was attempted (Atom too slow; a desktop cross-build of
  `libgallium` for the same Arch package would be the way).
- Why a draw in the same batch lowers the rate (AD/EB/HB, DevilutionX `full`).
- Rate variance between sessions and X11/Wayland (unknown factor).
- Which fraction of sway's shm uploads takes the busy (staging) path; needs an strace of sway or a
  wlroots debug build. Only the busy path is implicated.
- Hardware level: no documentation or prior bug found for CHV snooped GPU traffic stalling the display
  engine; fdo 105834 (GPD Pocket, only with a page-flipping/compositing client) is consistent with
  compositor uploads being the trigger there too.

## 8. Session notes

- At 21:12 the panel was off (idle, `power: false`); `swaymsg output DSI-1 power on` was issued to run
  the experiments (before the coordinator's "no power toggling" note arrived; not repeated). The first A
  run (21:08-21:10, 1 fps with the panel off) was discarded.
- The i915 resync budget (`vlv_dsi_resync_max=50`) was exhausted at 21:15; a LONE underrun occurred at
  21:35:53 during run AD, later PAIRs (last: #474, 22:07) realigned the pipe; the last classified lines at
  the end of the session were PAIRs.
- Scripts and raw data on the device: `/tmp/r4/runs*.sh`, `runs*.log`, `out-*.txt`, straces `st-*.txt`,
  batch dumps `bat-{A,B,AD}.txt`, sources `r4gl.c r4drm.c r4egl.c` (lost at reboot; desktop copies in the
  session scratch dir).
