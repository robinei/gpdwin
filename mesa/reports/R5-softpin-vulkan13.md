# R5: relocation overhead, softpin in a 32-bit PPGTT, and Vulkan 1.3 on hasvk (CHV) — 2026-10-12

Scope: ideas.md items 11-13 follow-up. Sources: Mesa 26.2.4 (our tree, branch `final`), Linux 7.2.9 i915
(`kernel/i915/build/src`), DXVK git (v2.0, v2.6.2, v2.7.1, master 3.1.1), Vulkan registry `vk.xml` (main).
Measurements on the GPD (kernel 7.2.9-arch1-1, `mesa-gpd`/`vulkan-intel-gpd` 1:26.2.4-1.3 installed; profiles
with the 1.4 build run from `/tmp/r5/mesa14` because only its debug package exists on the desktop). Test programs
in the desktop scratch dir `r5/` (`gp.c`, `vkq.c`, `eb.c`, `glb.c`, `vkb.c`) and on the device in `/tmp/r5/`; not in
the repo. Nothing was installed or changed on the device. Legend: **[M]** measured, **[S]** source-verified,
**[H]** hypothesis/estimate.

## 0. Summary

- **[M] Softpin works on CHV with this kernel.** A raw execbuf test with `EXEC_OBJECT_PINNED` at user-chosen
  addresses below 4 GiB (`I915_EXEC_NO_RELOC`, no relocation list) executes correctly (MI_STORE_DATA_IMM to pinned
  buffers, values verified, objects never moved). `I915_PARAM_HAS_EXEC_SOFTPIN = 1`, context GTT size = exactly
  4 GiB (`0x100000000`), full PPGTT (`I915_PARAM_HAS_ALIASING_PPGTT = 2`).
- **[S] Mesa's CHV exclusion is a policy choice in hasvk, not a kernel or hardware limit:** hasvk wants softpin
  only with 48-bit addresses (`gtt_size > 4 GiB`) because the gfx8/9 VF-cache workaround needs real addresses;
  in a 4 GiB space that workaround is trivially satisfied (the flushed condition "cached VB range > 4 GiB" cannot
  occur). iris rejects CHV outright and crocus never softpins.
- **[M] Relocation overhead is small in steady state.** With `I915_EXEC_NO_RELOC` and unchanged offsets the
  kernel does not touch relocation lists at all; the execbuf cost is the same with pinned or relocated objects
  (≈ 12 µs + ≈ 0.6 µs per object; 64 draws / 71 BOs / 327 relocs ≈ 50-60 µs of a 597 µs frame in crocus).
  Only when a buffer moves does the kernel rewrite relocations (≈ 1-1.3 µs each, with a stall on the batch).
  The userspace side (reloc list building, `presumed_offset` bookkeeping, hasvk's per-submit
  `anv_reloc_list_apply` with clflush on this non-LLC part) is a few percent of driver CPU time per batch.
  Softpin on i915 would not remove the per-object validation cost (that is VM_BIND territory, xe only).
- **[M/S] Vulkan 1.3 on hasvk/CHV:** every 1.3-required feature and extension is already reported **except
  `bufferDeviceAddress`** (required by 1.3 per `vk.xml`; hasvk ties it to softpin/A64 messages). The driconf
  `hasvk_report_vk_1_3_version` claim is therefore non-conformant by exactly that one feature.
  `descriptorIndexing` is not supported by hasvk on any gen (optional in 1.2/1.3).
- **[S] DXVK:** 2.0 – 2.6.x require a 1.3 `apiVersion` plus features CHV hasvk has → the claim-1.3 switch is
  enough for them. **DXVK 2.7+ (and 3.x) hard-require `bufferDeviceAddress` *and* `descriptorIndexing`,
  `runtimeDescriptorArray`, update-after-bind etc.** → not satisfiable by hasvk even with softpin, because
  descriptor indexing is missing on all hasvk gens. Real Vulkan 1.3 on CHV would only buy DXVK ≤ 2.6.x
  conformance-wise, which the driconf claim already delivers in practice.
- **Recommendation:** do nothing for GL/crocus; for DXVK use the existing driconf claim per game (DXVK ≤ 2.6.2,
  Wine's bundled DXVK version matters); keep "softpin in 32-bit" as a documented, unneeded option.

## 1. Facts about the device and kernel

[M] `gp.c` (`DRM_IOCTL_I915_GETPARAM` / `CONTEXT_GETPARAM`), device, 2026-10-12:

```
I915_PARAM_CHIPSET_ID = 8880 (0x22b0)      I915_PARAM_HAS_EXEC_SOFTPIN = 1
I915_PARAM_HAS_ALIASING_PPGTT = 2 (full)   I915_PARAM_HAS_LLC = 0
I915_PARAM_HAS_EXEC_NO_RELOC = 1           I915_PARAM_HAS_EXEC_HANDLE_LUT = 1
I915_PARAM_HAS_EXEC_BATCH_FIRST = 1        I915_PARAM_HAS_EXEC_FENCE_ARRAY / TIMELINE_FENCES = 1
I915_PARAM_MMAP_GTT_VERSION = 5            I915_PARAM_CMD_PARSER_VERSION = 0
CONTEXT_PARAM_GTT_SIZE = 0x100000000 (4096 MiB)   aperture 4096 MiB, mappable 4087 MiB
```

[S] Kernel side:
- `i915_pci.c:450-462` `chv_info`: `GEN(8)`, `has_64bit_reloc = 1`, `ppgtt_type = INTEL_PPGTT_FULL`,
  `ppgtt_size = 32` (BDW: 48). `gt/intel_ppgtt.c:315` `ppgtt->vm.total = BIT_ULL(ppgtt_size)` = 4 GiB;
  `gen8_ppgtt.c:995-999` comment: 4 PDP registers = 4 GiB legacy 32-bit space. `gem/i915_gem_context.c:2548`
  returns `vm->total` for `I915_CONTEXT_PARAM_GTT_SIZE` (hence exactly 4 GiB, not "> 4 GiB").
- `i915_getparam.c:145` `I915_PARAM_HAS_EXEC_SOFTPIN` is unconditionally 1. There is no gen/platform check on
  `EXEC_OBJECT_PINNED` anywhere in `gem/i915_gem_execbuffer.c`: `eb_validate_vma` (`:522-524`) only requires a
  page-aligned canonical offset, `eb_pin_flags` (`:429-430`) turns it into `PIN_OFFSET_FIXED`, `eb_pin_vma`
  (`:438-470`) pins at that offset and fails instead of relocating when it cannot. Without
  `EXEC_OBJECT_SUPPORTS_48B_ADDRESS` an object must satisfy `(start + size + 4095) >> 32 == 0`
  (`eb_vma_misplaced` `:400-402`, `PIN_ZONE_4G` `:423-424`) — automatically true in a 4 GiB VM.
- `gen8_init_rsvd` (`gen8_ppgtt.c:962-991`) reserves one page at the top of the VM only for
  `intel_gt_needs_wa_16018031267` (Xe-LPG+), not on CHV.
- The relocation fast path: `i915_gem_do_execbuffer` sets `__EXEC_HAS_RELOC` only when userspace does *not*
  pass `I915_EXEC_NO_RELOC` (`:3357-3358`) or when a pinned object's offset differs from its current one
  (`eb_validate_vmas` `:1018-1021`, `eb_reserve_vma` `:676-679`). `eb_relocate_parse` (`:1914-1924`) walks
  relocation lists only if that flag is set. So with NO_RELOC and stable offsets the kernel **never reads the
  relocation arrays**; the per-execbuf work is `eb_lookup_vmas` + `eb_validate_vmas` (lock + pin each object)
  + `eb_parse` + request submission, identical for pinned and unpinned objects.
- When it does relocate: `eb_relocate_vma` (`:1511-1593`) copies the reloc entries in 512-byte chunks,
  `eb_relocate_entry`/`relocate_entry` (`:1377-1420`) writes each 64-bit address through `reloc_vaddr`:
  on this non-LLC part uncached batch BOs take `reloc_iomap` (`:1292-1360`): `set_to_gtt_domain(write)` (waits
  for the GPU to finish with the batch), a GGTT aperture mapping per page, `io_mapping_map_atomic_wc`, and
  `intel_gt_flush_ggtt_writes` on every page change; snooped/dirty objects take `reloc_kmap` with clflush.

## 2. Relocation overhead (idea #4 / question 1)

### 2a. Raw execbuf microbenchmark `eb.c` [M]

Batch = R × `MI_STORE_DATA_IMM` into K 4-KiB target BOs + `MI_BATCH_BUFFER_END`, submitted N times on the
render engine (`I915_EXEC_HANDLE_LUT`), ioctl timed with `clock_gettime`, CPU split from `getrusage`. Stored
values are verified at the end (so correctness is checked, not just `errno`). Modes: `softpin` =
`EXEC_OBJECT_PINNED` at 0x40000000 + i·64 KiB (targets) and 0x80000000 (batch), NO_RELOC, no reloc list;
`noreloc` = reloc list with correct `presumed_offset` + NO_RELOC (what crocus/hasvk do in steady state);
`check` = same list without NO_RELOC (kernel walks and compares every entry); `write` = `presumed_offset = -1`
each time (kernel rewrites every relocation).

| K objects | R relocs | mode | ioctl mean / median µs | stime µs/iter | result |
|---|---|---|---|---|---|
| 1 | 1 | softpin | 16.8 / 13.9 | 11.9 | ok |
| 1 | 1 | noreloc | 16.2 / 14.2 | 12.6 | ok |
| 16 | 64 | softpin | 26.9 / 21.5 | 21.1 | ok, stores verified, nothing moved |
| 16 | 64 | noreloc | 27.2 / 22.7 | 20.3 | ok |
| 16 | 64 | check | 23.9 / 22.7 | 20.1 | ok |
| 16 | 64 | write | 111.3 / 105.7 | 51.8 | ok (wall ≫ CPU: `set_to_gtt_domain` waits for the batch) |
| 64 | 512 | softpin | 139 / 75 (p99 2.4 ms: queue throttling) | 78 | ok |
| 64 | 512 | noreloc | 64 / 50 | 50 | ok |
| 64 | 512 | check | 70 / 63 | 64 | ok |
| 64 | 512 | write | 548 / 519 | 260 | ok |
| 128 | 1 | softpin | 102 / 99 | 90 | ok |
| 128 | 1 | noreloc | 90 / 86 | 86 | ok |

Reading: base cost ≈ 12-14 µs per execbuf, ≈ 0.6-0.7 µs per object (both modes), walking an unchanged
relocation list ≈ 0.01 µs per entry, rewriting one ≈ 1-1.3 µs plus a CPU↔GPU serialisation on the batch. The
softpin median is not lower than NO_RELOC steady state (if anything a few µs higher at 64-128 objects; within
noise/pinning differences). Softpin is also verified functionally: 2000+ batches at pinned addresses in the
1-2 GiB range, every store landed, `obj.offset` unchanged.

### 2b. crocus (GL) `glb.c`, surfaceless EGL/GLES2, 256² FBO, tiny textured triangles [M]

64 draws per frame cycling through 32 textures + 32 VBOs (so each draw adds relocations), `glFlush` every
B draws forces a batch. Installed Mesa 1.3. `INTEL_DEBUG=bat`: a 64-draw batch = 6.9 KB of commands,
**71 BOs, 134 command relocs + 193 state relocs**; an 8-draw batch = 23 BOs, 22 + 25 relocs.

| draws / batch | per frame wall / utime / stime µs | per batch wall / utime / stime µs | per draw µs |
|---|---|---|---|
| 64 (1 batch) | 597 / 386 / 187 | 597 / 386 / 187 | 9.3 |
| 8 (8 batches) | 964 / 555 / 363 | 120 / 69 / 45 | 15.1 |
| 1 (64 batches) | 3798 / 1537 / 2029 | 59 / 24 / 32 | 59.3 |
| 64, only 4 textures/VBOs | 417 / 349 / 57 | | 6.5 |

Per batch the kernel time is ≈ 32-45 µs for small batches (execbuf ≈ 20 µs + the other per-batch ioctls:
`strace` shows per batch 1 EXECBUFFER2, ~4 GEM_MADVISE, ~2 GEM_BUSY, 1 SYNCOBJ_CREATE + 1 SYNCOBJ_DESTROY) and
≈ 187 µs for the 71-BO batch, of which the execbuf itself (from 2a: 12 + 71 × 0.65) ≈ 60 µs; the rest is
MADVISE/BUSY/syncobj and page faults on the fresh batch/state BO maps. What the relocation *mode* adds on top:
kernel ≈ 0 (steady state), userspace = `emit_reloc` (`crocus_batch.c:376-430`: `crocus_use_bo`,
`find_exec_index`, append entry, write presumed address) per reloc ≈ 327 entries per 64-draw batch; see the
profile below. [H] From the per-draw difference 32 vs 4 distinct textures (9.3 vs 6.5 µs) only part is relocs
(the rest is surface-state/binding-table emission and BO bookkeeping that softpin keeps).

[M] `perf record -e cpu-clock:u` (user space only; `perf_event_paranoid = 2`) of `glb` with the 1.4 build and its
debug symbols, whole process (GL state tracker + crocus):

| symbol | 64 draws, 8 per batch | 64 draws, 1 batch |
|---|---|---|
| `crocus_upload_render_state` | 4.8 % | 4.9 % |
| `isl_gfx8_surf_fill_state_s` | 3.0 % | 3.6 % |
| `crocus_update_compiled_shaders` | 3.3 % | 3.0 % |
| **`emit_reloc`** (reloc list append + presumed address) | 1.2 % | 2.5 % |
| `crocus_use_bo` (validation-list entry, needed with softpin too) | 2.2 % | 1.7 % |
| `crocus_command_reloc`/`crocus_state_reloc` | 0.1 % | 0.2 % |
| `_crocus_batch_flush` | 1.5 % | 0.2 % |
| `ioctl` (libc) | 1.7 % | — |

So the relocation-specific user-space work is ≈ 1.3-2.7 % of the process' user time: with 381 µs utime per
64-draw batch that is ≈ 5-10 µs per batch (≈ 30 ns per relocation, 327 of them), plus the validation list
bookkeeping that stays with softpin. Kernel side in steady state: nothing (section 1).

### 2c. hasvk (Vulkan) `vkb.c`, headless, one command buffer with R `vkCmdCopyBuffer` over 32 buffers [M]

Submitted N times, fence wait every 8 submits (so wall includes GPU time; CPU = utime + stime).

| copies | per submit wall / utime / stime µs |
|---|---|
| 8 | 195 / 20 / 79 |
| 64 | 654 / 99 / 205 |
| 256 | 1123 / 339 / 209 |
| 64, `HASVK_USERSPACE_RELOCS=0` (kernel relocates surface states) | 897 / 45 / 260 |

In relocation mode hasvk re-applies **all surface-state relocations in userspace on every submit**
(`anv_batch_chain.c:1526-1549` `relocate_cmd_buffer` → `anv_reloc_list_apply(..., always_relocate=true)`,
`write_reloc` with clflush on non-LLC) and adds every reloc target to the execbuf by hash lookup
(`anv_execbuf_add_bo` `:1316-1337`); with `HASVK_USERSPACE_RELOCS=0` the same work moves to the kernel
(`presumed_offset = -1`, `reset_cmd_buffer_surface_offsets` `:1552-1567`): utime −54 µs, stime +55 µs. So the
relocation-mode cost of this 64-copy submit is ≈ 55-75 µs of ≈ 300 µs CPU (≈ 18-25 %). [M] `perf` (user space,
1.4 build with symbols, 116 µs utime per submit): `util_flush_range` 21 % + `util_flush_range_no_fence` 14 %
(the clflush of every rewritten surface-state address on this non-LLC part), `anv_execbuf_add_bo` 17 % (one
call per relocation target), `anv_reloc_list_apply` 13 %, `anv_queue_exec_locked` 12 %: ≈ 65 % of hasvk's
user-space submit time is relocation-mode work. With softpin hasvk also
chains command buffers (`anv_cmd_buffer_is_chainable` `anv_private.h:2597-2601`), uses `use_call_secondary`
and the separate binding-table pool, and skips the surface-state reloc list entirely.

### 2d. Is it a meaningful fraction of a game frame? [H]

A 60 fps frame is 16.7 ms. On this device a batch with 64 draws and 71 BOs costs ≈ 0.6 ms of CPU in crocus,
of which the relocation mode itself is of the order of 20-40 µs (userspace list building; kernel ≈ 0). Even a
heavy frame with 5 batches / 1500 relocs would spend well under 0.2 ms (≈ 1 % of the frame) on relocation
mode. The dominating per-batch costs (object validation ≈ 0.65 µs/BO, MADVISE/BUSY/syncobj ioctls, state
emission) stay the same with softpin on i915. For hasvk the share is higher (≈ 15-20 % of submit CPU in the
copy test, because surface states are re-relocated with clflush every submit), but hasvk is only used by SDL's
`vulkan`/`gpu` renderers and Wine/DXVK here.

## 3. Softpin in a 32-bit PPGTT (question 2)

### 3a. Kernel: allowed [S][M]
See section 1: no platform check, `HAS_EXEC_SOFTPIN = 1`, pinned objects accepted and executed correctly at
addresses in [1 GiB, 2 GiB] and 2 GiB (`eb.c` softpin runs). Any page-aligned address in [4 KiB, 4 GiB) works;
userspace must keep its own `drm_mm` (util_vma_heap) and must not exceed `vm.total` (pin fails with
`-ENOSPC`/`-EINVAL`, which the kernel reports instead of relocating because `EXEC_OBJECT_PINNED` disables the
fallback: `eb_pin_vma` `:461-462`).

### 3b. hasvk: why CHV is excluded, and what the "vertex cache flush workaround" is [S]
- `anv_device.c:1693-1694`: `use_relocations = ver < 8 || platform == CHV`; `:1749-1753`:
  `supports_48bit_addresses = ver >= 8 && gtt_size > 4 GiB` ("We only allow 48-bit addresses with softpin
  because knowing the actual address is required for the vertex cache flush workaround"); `:1759-1760` asserts
  the two agree and sets `use_softpin`. CHV: `gtt_size == 4 GiB` → both false. `anv_private.h:1075-1087`
  comment: "Broadwell softpins but Cherryview doesn't". Upstream made softpin mandatory for gen8+ in
  [MR 4431](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/4431) with CHV kept on relocations.
- The workaround (`genX_cmd_buffer.c:4860-4955`, `anv_private.h:2332-2360`
  `anv_gfx8_9_vb_cache_range_needs_workaround`): the gfx8/9 VF cache tags by the low 32 address bits, so
  overlapping-mod-4 GiB vertex/index buffer ranges alias; hasvk tracks the union of all bound VB/IB ranges since
  the last VF invalidate and emits `CS_STALL | VF_CACHE_INVALIDATE` when `dirty->end - dirty->start > 2^32`.
  In a 4 GiB address space every address is < 2^32, so the union can never exceed 2^32 and the flush condition
  is never true: **the workaround is satisfied for free in 32-bit softpin**; the code can simply keep running
  (bound ranges are computed from the pinned addresses) or be skipped. Nothing else in hasvk depends on
  48-bit except address-space layout.
- What has to change for 32-bit softpin in hasvk:
  1. `anv_private.h:150-180`: the pool/heap layout is 48-bit (general state 2 MiB-1 GiB, low heap 1-2 GiB,
     dynamic 3-4 GiB, binding tables 4-5 GiB, surface states 5-6 GiB, instructions 6-7 GiB, client-visible
     7-11 GiB, high heap 11 GiB … gtt_size − 4 GiB; `anv_device.c:2540-2558`, `:2623-2633`). A 4 GiB layout
     must squeeze all of it below 4 GiB with the constraints: general/dynamic/surface/instruction state pools
     each within 4 GiB of their base (trivial), binding-table pool below surface-state pool
     (`bt_pool_offset` negative 32-bit, `:2624-2626`), page 0 unused, and "the last 4 GiB withheld so that base
     + size cannot overflow 48 bits" becomes "must not overflow 32 bits", i.e. leave a margin at the top. The
     block pools grow in place up to their reserved range (`anv_allocator.c` softpin path), so the ranges
     decide the maximum state size: e.g. 2 MiB-64 MiB general, 64-320 MiB dynamic, 320-384 MiB binding
     tables, 384-640 MiB surface states, 640-896 MiB instructions, 896 MiB-1 GiB client-visible (BDA
     capture/replay; could be disabled), 1 GiB-~3.9 GiB high/low heap for everything else.
  2. `anv_compute_sys_heap_size` (`:1287-1308`): today clamps the Vulkan heap to **2 GiB** on this device
     (warning printed at every device creation); with softpin the heap must fit the ≈ 2.9 GiB of address space
     left after the pools, which is also an upper bound on resident Vulkan memory per process (3.7 GB RAM, so
     ≈ 2.5-2.9 GiB is realistic).
  3. `supports_48bit_addresses`/`EXEC_OBJECT_SUPPORTS_48B_ADDRESS` must stay off (fine: `anv_bo_alloc_flags_to_bo_flags`
     `anv_allocator.c:1511-1535` adds `PINNED` from `use_softpin`, 48B from `supports_48bit_addresses`), and
     `intel_48b_address`/canonical-address helpers are no-ops below 4 GiB.
  4. Everything gated on `!use_relocations` then switches on automatically: `has_a64_buffer_access` →
     `KHR/EXT_buffer_device_address` (`anv_device.c:181, 257, 452-453`), 64-bit global SSBO/UBO addressing
     (`anv_nir.h:38-62`, `anv_nir_apply_pipeline_layout.c:913`: constant data via `load_global_constant`),
     `use_call_secondary`, command-buffer chaining, `max_ssbos = UINT16_MAX` (`:957`), binding-table pool,
     and `apiVersion`/`conformanceVersion` 1.3 (`:766`, `:979`). All of these are the BDW code paths, already
     exercised by hasvk on Broadwell, but **never tested on CHV**.
  5. The two Gen8 tests in `src/intel/vulkan_hasvk/tests` (`block_pool_grow_first`, `state_pool_padding`) use
     `use_softpin = true` with the 48-bit constants; they would need the new layout too.
- Risk: GPU hangs from any BDW-only assumption in the A64/softpin paths (CHV differs from BDW in a few places:
  `gfx8_cmd_buffer.c:186` CHV line width field, `crocus_state.c:2029`), address-space exhaustion (ENOSPC on
  pin → `VK_ERROR_OUT_OF_DEVICE_MEMORY`), and the general unknown of running BDA shaders on CHV's EUs.
  Conformance would need the Vulkan CTS 1.3 run on this device (days of runtime on an Atom; dEQP-VK is
  ~500k tests).

### 3c. crocus: no softpin at all [S]
`crocus_bufmgr.c` has no VMA allocator (`util/vma.h` is included but unused); `crocus_batch.c:906` asserts no
BO is ever `EXEC_OBJECT_PINNED`; every address emitted goes through `crocus_command_reloc`/`crocus_state_reloc`
(`:432-451`). Adding softpin to crocus means: a per-screen `util_vma_heap` per memzone (iris layout in
`iris_bufmgr.h:30-58` and `iris_bufmgr.c:2409`, which **requires `gtt_size > 12 GiB`** and
`iris_bufmgr.c:2662` explicitly refuses `ver < 8 || CHV`), assigning addresses at BO creation, replacing all
reloc emission sites with direct address writes (dozens of call sites in `crocus_state.c`, `crocus_blorp.c`,
`crocus_draw.c`, …), the same 32-bit layout constraints as above, and keeping relocation mode for gen4-7 (crocus
is the gen4-8 driver, so it would be a second code path everywhere). That is a large project for the measured
gain in 2b (a few % of batch CPU) and there is no upstream interest (crocus is maintenance-only).

### 3d. Precedent
iris = gen8+ softpin driver with the memzone design above, but it refuses CHV and 4 GiB GTTs, and uses
`EXEC_OBJECT_SUPPORTS_48B_ADDRESS` everywhere. hasvk on Broadwell is the real precedent: the only
hardware-dependent part is the VF cache workaround, which 32-bit addressing makes a no-op.

## 4. Vulkan 1.3 on hasvk/CHV (question 3)

### 4a. What is reported today [M] (`vkq.c`, installed 1.3 build)
`api 1.2.354`, `conformanceVersion 1.2.0.0`, driver "Intel open-source Mesa driver"; with the driconf claim
(the shipped `/usr/share/drirc.d/00-hasvk-defaults.conf` enables `hasvk_report_vk_1_3_version` for
`BrawlhallaGame.exe`; tested by naming the binary that) `api 1.3.354`, conformance still 1.2.0.0, features and
extensions otherwise identical (127 device extensions, including all 1.3-promoted ones: dynamic_rendering,
synchronization2, maintenance4, copy_commands2, format_feature_flags2, shader_integer_dot_product,
zero_initialize_workgroup_memory, shader_terminate_invocation, shader_demote_to_helper_invocation,
private_data, pipeline_creation_cache_control, subgroup_size_control, inline_uniform_block, image_robustness,
texel_buffer_alignment, 4444_formats, extended_dynamic_state(2), shader_non_semantic_info, tooling_info).

### 4b. Required by Vulkan 1.3 vs. hasvk CHV [S][M]
`vk.xml` `<feature name="VK_VERSION_1_3"><require comment="Feature requirements">` (unconditional):
shaderTerminateInvocation, shaderDemoteToHelperInvocation, privateData, pipelineCreationCacheControl,
synchronization2, shaderZeroInitializeWorkgroupMemory, robustImageAccess, subgroupSizeControl,
computeFullSubgroups, dynamicRendering, shaderIntegerDotProduct, maintenance4, vulkanMemoryModel,
vulkanMemoryModelDeviceScope, inlineUniformBlock, **bufferDeviceAddress**; plus 1.2's unconditional set
(subgroupBroadcastDynamicId, imagelessFramebuffer, uniformBufferStandardLayout, shaderSubgroupExtendedTypes,
separateDepthStencilLayouts, hostQueryReset, timelineSemaphore) and 1.1's (multiview,
storageBuffer16BitAccess, shaderDrawParameters). `descriptorBindingInlineUniformBlockUpdateAfterBind` is only
required if descriptorIndexing is supported.

hasvk CHV reports all of them as 1 **except `bufferDeviceAddress = 0`** (`anv_device.c:452`
`= pdevice->has_a64_buffer_access`, which is `ver >= 8 && use_softpin`, `:1783`). `descriptorIndexing = 0`
(`:422`, hard-coded false for all hasvk gens) is allowed. So:
- the only 1.3 gap is BDA, and BDA in hasvk is implemented with A64 (global) memory messages whose addresses the
  shader needs at compile/record time → softpin. In 32-bit softpin the addresses are simply < 4 GiB; the A64
  messages work with any 48-bit canonical address, so nothing in the BDA implementation is 48-bit specific
  (`anv_nir.h`, `nir_address_format_64bit_global*`).
- `hasvk_report_vk_1_3_version` = claim 1.3 while BDA is false: an application that reads
  `VkPhysicalDeviceVulkan12Features::bufferDeviceAddress` sees 0 and must not use BDA; one that assumes "1.3
  ⇒ BDA" without checking breaks. DXVK ≤ 2.6.x checks (section 4c), DXVK ≥ 2.7 requires it. Not CTS-conformant,
  not a stability problem by itself.
- CTS plausibility of a real 1.3 (softpin) hasvk on CHV: hasvk on BDW is listed as 1.3-conformant; CHV runs
  the same gfx8 code with `platform == CHV` differences in a handful of places. [H] Likely mostly passing,
  but a full CTS run and hang hunting are the actual work (weeks, as ideas.md says).

### 4c. DXVK requirements [S] (fetched from github.com/doitsujin/dxvk)
- Version gate: `dxvk_device_filter.cpp` (v2.0 `:20`, v2.6.2 `:39`): skips adapters with
  `apiVersion < 1.3`; master `DxvkVulkanApiVersion = VK_API_VERSION_1_3` (`dxvk_include.h:12`). The instance
  is created with `apiVersion 1.3` (`dxvk_instance.cpp:166`). → the driconf claim is the minimum for any 2.x.
- DXVK 2.0 hard requirements (`dxvk_adapter.cpp` createDevice `= VK_TRUE` lines, `dxvk_extensions.h` Required):
  robustBufferAccess, vulkanMemoryModel, hostQueryReset, timelineSemaphore, synchronization2,
  dynamicRendering, `VK_EXT_robustness2` (robustBufferAccess2, nullDescriptor), `VK_KHR_swapchain`.
- DXVK 2.6.2 (`dxvk_adapter.cpp:327-430`, `dxvk_extensions.h`): + multiDrawIndirect, shaderDrawParameters,
  maintenance4 (except AMD); `bufferDeviceAddress` only for the NVX CUDA interop path (`:284-294`, optional).
- DXVK 2.7 / 2.7.1 / 3.x (`dxvk_device_info.cpp` `getFeatureList`, `ENABLE_FEATURE(..., true)` = required,
  `checkDeviceCompatibility` `:629-655`): required core features now include **bufferDeviceAddress,
  descriptorIndexing, descriptorBindingSampledImageUpdateAfterBind, descriptorBindingUpdateUnusedWhilePending,
  descriptorBindingPartiallyBound, runtimeDescriptorArray**, shaderInt64, shaderSampledImageArrayDynamicIndexing,
  samplerMirrorClampToEdge, uniformBufferStandardLayout, maintenance4, shaderDemoteToHelperInvocation,
  shaderZeroInitializeWorkgroupMemory, synchronization2, dynamicRendering, plus `VK_KHR_pipeline_library` and
  `VK_KHR_swapchain` as required extensions (`:878`, `:885`) and `maxPushConstantsSize ≥ MaxTotalPushDataSize = 256`
  (`dxvk_limits.h:22`; hasvk reports 128, `anv_private.h:211`, `anv_device.c:998`).
- hasvk CHV against these [M] (`vkq-default.txt`): DXVK 2.0-2.6.x: all required features/extensions present
  (robustBufferAccess2/nullDescriptor are true in `anv_device.c:562-564`; multiDrawIndirect, shaderDrawParameters,
  maintenance4, pipeline_library absent but optional there) → **works with the claim-1.3 driconf**, which is
  exactly what upstream added it for (Brawlhalla ships DXVK). DXVK ≥ 2.7: **fails on descriptorIndexing /
  runtimeDescriptorArray / update-after-bind (not implemented in hasvk at all), on bufferDeviceAddress, on
  `VK_KHR_pipeline_library` (not exposed) and on 256 bytes of push constants (hasvk: 128)** → softpin alone
  would not help; descriptor indexing on gfx8 is a
  separate large feature (bindless surface states; hasvk has `always_use_bindless`/`has_bindless_samplers`
  plumbing but no descriptor-indexing support).

## 5. Options (question 4)

| # | option | gain | risk | effort | how to test safely |
|---|---|---|---|---|---|
| 1 | **Do nothing** (GL stays relocation mode) | — | none | 0 | — |
| 2 | **Claim 1.3 per game via driconf** (`hasvk_report_vk_1_3_version=true` in a `~/.drirc`/`/etc/drirc` `<application executable="…">` entry, or a game-specific `MESA_VK_…`-free approach: driconf only) | DXVK 2.0-2.6.x games run on hasvk (D3D9/10/11 in Wine); nothing for GL | app that assumes BDA from 1.3 breaks (DXVK ≤ 2.6 checks; DXVK ≥ 2.7 refuses the device with a clear log line "Device does not support required feature 'bufferDeviceAddress'"); non-conformant claim | minutes | run the game from a terminal with `DXVK_LOG_LEVEL=info`: the log prints the adapter and either "Skipping Vulkan 1.2 adapter" (claim off) or the device-compatibility error; check the DXVK version bundled with the Wine build first (`dxvk_*` DLL version resource or `DXVK_HUD=version`) |
| 3 | **Softpin in 32-bit for hasvk** (patch `anv_device.c:1693-1753` + new `anv_private.h` layout + heap size; everything else switches automatically) | real BDA/1.3 features, −15-20 % submit CPU in hasvk, command-buffer chaining; still no DXVK ≥ 2.7 (descriptor indexing, push-constant size) | GPU hangs in untested BDW-only paths on CHV, 2.9 GiB address-space cap, no CTS run | ~1-2 days for a PoC, weeks for conformance | PoC build run from `/tmp` (`LD_LIBRARY_PATH`/`VK_DRIVER_FILES` as `mesa/bench.sh` does), headless first (`vkq`, `vkb`, then dEQP-VK subsets like `dEQP-VK.api.buffer*`, `*.memory.*`, `*.binding_model.*` with `--deqp-surface-type=pbuffer`), then SDL `vulkan` renderer games; never install until the hang counter (`cat /sys/kernel/debug/dri/*/i915_gpu_info` needs root; `journalctl -k | grep -i 'GPU HANG'`) stays at 0 |
| 4 | Softpin for crocus | ≤ a few % batch CPU; no feature gain | large new code path in a maintenance-only driver; hangs | weeks | not recommended |
| 5 | Reduce per-batch kernel work instead (what actually costs): fewer BOs per batch (BO cache reuse, suballocation of small VBOs/UBOs — crocus already suballocates uploads), fewer `GEM_BUSY`/`MADVISE` calls, fewer flushes per frame (SDL `opengl` renderer flushes per `SDL_RenderPresent` only; check games that `glFlush` per draw) | up to ~0.1-0.3 ms per frame in draw-heavy frames [H] | low | days, per game | `strace -c -e ioctl` and `INTEL_DEBUG=bat` on the game (`flush with … BOs, … relocs` lines) |

Ranking for this device: **2 (when a DXVK ≤ 2.6 game needs it) > 1 > 5 > 3 ≫ 4.** Option 3 is only worth it if a
Vulkan-native game or an SDL `gpu`-renderer title needs BDA, and only after checking the game's actual
requirements with `vkq`-style feature dumps rather than its minimum "Vulkan 1.3" claim.

## 6. Open points / not measured
- No on-screen game profile this round (the screen was in use); the relocation share per real game frame is an
  estimate from the microbenchmarks and the per-batch reloc counts (`INTEL_DEBUG=bat` on a game would give exact
  BO/reloc counts; cheap to add to a future on-screen session).
- `perf` kernel-side samples are not available without root (`perf_event_paranoid = 2`); kernel costs come from
  `getrusage` stime, `strace -T` (inflated by ptrace) and `eb.c` ioctl timing.
- The Mesa 1.4 build profiled here carries new crocus upload patches (uncommitted in the repo at the time);
  they do not touch batch submission. The device was rebooted by the main session at ~01:53 (its `/tmp` was
  cleared); the `perf` profiles are from the fresh boot, the other measurements from before.
