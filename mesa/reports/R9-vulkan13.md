# R9: real Vulkan 1.3 on hasvk/Cherry Trail (32-bit softpin, buffer device address) — 2026-10-12

Scope: ideas.md item 13 / R5 section 3b-4 follow-up: make hasvk (Mesa 26.2.4, `src/intel/vulkan_hasvk`) report and
implement Vulkan 1.3 on this device (Atom x7-Z8700, gfx8 CHV, 4 GiB PPGTT) instead of the `hasvk_report_vk_1_3_version`
claim, and see how far that gets DXVK >= 2.7. Sources: Mesa 26.2.4 (`mesa/mesa-26.2.4.tar.xz` + our patches 0001-0006,
scratch tree branch `vk13`), DXVK v2.7.1 (github), VK-GL-CTS `opengl-es-cts-3.2.14.1` (its `external/vulkancts`, i.e. the
Vulkan CTS of the same tree). Legend: **[M]** measured, **[S]** source-verified, **[H]** hypothesis/estimate.

Nothing was installed on the GPD: everything ran from `~/vk13-test` (driver `.so` + ICD json selected with
`VK_DRIVER_FILES`), the installed driver is `vulkan-intel-gpd 1:26.2.4-1.7` throughout and is the "stock" reference below.
Patches: `mesa/vk13/0001-0004` (not wired into `mesa/PKGBUILD`, see `mesa/vk13/README.md`). Test scripts:
`mesa/reports/r9/`.

## 0. Summary

- **Implemented, not aborted.** Four patches, **332 insertions / 76 deletions** (`git diff --stat`; 314/70 of them
  driver code, the rest the unit tests' extra argument). Patch 1 (the 32-bit layout) is 224/49. No change to hasvk's
  relocation/allocator design: the existing Broadwell softpin code runs on CHV with a second, 32-bit address layout.
- **[M] Vulkan 1.3 is real on CHV now:** `apiVersion 1.3.354`, `bufferDeviceAddress = 1`,
  `VK_KHR/EXT_buffer_device_address` exposed, `conformanceVersion 1.3.0.0` (upstream's own logic for softpin devices; CHV
  has of course not been through a conformance submission, see section 6/8).
- **[M] dEQP-VK:** `binding_model.buffer_device_address.*` **4716 / 4716 pass** on the assert-enabled final build.
  Every other sampled group identical to the installed driver, test by test (section 4), **0 GPU hangs, 0 asserts**.
  Push constants: the 256-byte cases that are NotSupported on stock pass. One hang found and fixed on the way
  (patch 2: pool exhaustion spun forever; `allocate_many_primary` with 10000 command buffers).
- **[M] DXVK 2.7.1 runs on this driver:** with patch 4's opt-in `hasvk_report_descriptor_indexing=true` the D3D9 test
  (`r5/d3d9tex.exe`, 64-bit Wine) renders correctly (textured + untextured quads, readback checked, 300 and 600
  frames), assert build, no hang. Without the option DXVK 2.7.1 stops at `Device does not support required feature
  'descriptorIndexing'` (so the 1.3 + BDA + push constant + pipeline_library part is verified by DXVK's own checks too).
- **[M] Performance:** submit CPU time in the copy benchmark (`r5/vkb`) **-38 % user, -13 % sys** (relocation work gone);
  GPU-bound fill test (`r5/vkdepth`) unchanged (12.1 vs 12.2 ms); dEQP group wall times equal.
- Stretch pieces for DXVK >= 2.7 (section 7): 256-byte push constants and `VK_KHR_pipeline_library` are patch 3
  (8 lines, CTS-tested). Descriptor indexing is **not** implementable conformantly on gfx8 (no bindless surfaces, 240-entry
  binding tables vs the spec's 500000 minimums), so patch 4 only *reports* the features on request; DXVK's single use of
  them (a 2048-entry update-after-bind sampler heap) maps onto hasvk's existing bindless-sampler path and works.
  A real implementation of what *can* be done on gfx8 is estimated at 300-500 lines (section 7.3).
- **For Robin to decide:** ship patches 1-3 (add to `mesa/PKGBUILD` + `lib32`, rebuild, on-screen checks of the Vulkan
  users; `HASVK_USE_RELOCATIONS=1` is the per-game escape hatch) and whether a DXVK >= 2.7 build with patch 4's option is
  wanted for any game (the installed games use DXVK 2.4.1, which does not need any of this).

## 1. Gap list: Vulkan 1.3 requirements vs hasvk on CHV [S][M]

Checked against `vk.xml` (`VK_VERSION_1_3`/`1_2`/`1_1` `<require comment="Feature requirements">`) and
`anv_device.c:get_features()` / `get_extensions()` / `get_properties()` of 26.2.4, and against the `r5/vkq` dump of the
installed driver:

| requirement | hasvk CHV (stock) | why |
|---|---|---|
| 1.1 core features: multiview, storageBuffer16BitAccess, shaderDrawParameters | yes | |
| 1.2: subgroupBroadcastDynamicId, imagelessFramebuffer, uniformBufferStandardLayout, shaderSubgroupExtendedTypes, separateDepthStencilLayouts, hostQueryReset, timelineSemaphore | yes | |
| 1.3: shaderTerminateInvocation, shaderDemoteToHelperInvocation, privateData, pipelineCreationCacheControl, synchronization2, shaderZeroInitializeWorkgroupMemory, robustImageAccess, subgroupSizeControl, computeFullSubgroups, dynamicRendering, shaderIntegerDotProduct, maintenance4, vulkanMemoryModel(+DeviceScope), inlineUniformBlock | yes | |
| **1.3: bufferDeviceAddress** | **no** | `= pdevice->has_a64_buffer_access = ver >= 8 && use_softpin` (`anv_device.c:452, 1782`); `use_relocations = ver < 8 \|\| platform == CHV` (`:1693`) |
| 1.3 promoted extensions (dynamic_rendering, synchronization2, maintenance4, copy_commands2, format_feature_flags2, shader_integer_dot_product, zero_initialize_workgroup_memory, shader_terminate_invocation, shader_demote_to_helper_invocation, private_data, pipeline_creation_cache_control, subgroup_size_control, inline_uniform_block, image_robustness, texel_buffer_alignment, 4444_formats, extended_dynamic_state(2), shader_non_semantic_info, tooling_info) | all exposed | 127 device extensions in the stock dump |
| 1.3 limits/properties (`VkPhysicalDeviceVulkan13Properties`: subgroup sizes, inline uniform block, texel buffer alignment, maxBufferSize ...) | filled by the same code as Broadwell | `get_properties()` has no CHV/softpin conditionals except `conformanceVersion.minor` and `apiVersion` |
| optional in 1.3 and absent: descriptorIndexing (all gens), samplerFilterMinmax, shaderFloat16/Int8 only with 16-bit enabled, textureCompressionASTC_HDR | allowed | |

So R5's conclusion holds: **the only 1.3 gap is bufferDeviceAddress**, and in hasvk that feature (A64 memory messages
with real GPU addresses in the shader) is tied to softpin. `apiVersion` and `conformanceVersion` switch to 1.3 automatically
with `use_softpin` (`anv_device.c:766, 979`).

What else switches on with softpin on gfx8 (all Broadwell code paths, none of them CHV-specific) [S]:
`has_a64_buffer_access` (BDA extensions, SSBO/UBO access through `nir_address_format_64bit_*` instead of binding-table
index/offset (`anv_nir.h:38-62`), shader constant data through `load_global_constant`, `max_ssbos = 65535` instead of 64),
`use_call_secondary` (secondary command buffers executed with `MI_BATCH_BUFFER_START` instead of being copied), command
buffer chaining across `vkQueueSubmit` batches, the separate binding-table pool with `STATE_BASE_ADDRESS` re-emission
per 64 KiB block, surface states without relocation lists (no per-submit `anv_reloc_list_apply` + clflush, R5 2c), and every
user memory object being listed in every execbuf (`anv_batch_chain.c:1786-1794`).

## 2. Design: 32-bit softpin [S]

What blocks softpin on CHV is only the address layout: hasvk's VMA map (`anv_private.h:150-180`) needs ~15 GiB of a
48-bit space (pools at 1 GiB spacing, client-visible heap at 7-11 GiB, high heap above). CHV's PPGTT is exactly 4 GiB
(`I915_CONTEXT_PARAM_GTT_SIZE = 0x100000000`, R5 section 1) and its BOs cannot carry `EXEC_OBJECT_SUPPORTS_48B_ADDRESS`,
which hasvk's `supports_48bit_addresses` already encodes (`gtt_size > 4 GiB`). The kernel pins at any page-aligned address
below 4 GiB (R5: `eb.c`, measured). Constraints that the 48-bit layout satisfies and the new one must too:

1. the gfx8 VF-cache workaround (`anv_gfx8_9_vb_cache_range_needs_workaround`, invalidate when bound vertex/index ranges
   span more than 4 GiB): trivially satisfied, every address is below 4 GiB; the tracking code keeps running unchanged;
2. binding tables are 16-bit offsets and surface states 32-bit offsets from Surface State Base Address, which hasvk sets to
   the current 64 KiB binding-table block: the binding table pool must lie *below* the surface state pool and within 4 GiB
   (`bt_pool_offset` negative `int32`, `anv_device.c:2624`);
3. `STATE_BASE_ADDRESS` sizes: with softpin hasvk programs the real pool sizes for dynamic/instruction state
   (`genX_cmd_buffer.c:154`), so the pools need fixed maximum sizes;
4. the general state base address stays 0 (scratch is relative to it), the pool starts at 2 MiB as before;
5. a margin below the 4 GiB boundary (the 48-bit layout withholds the top 4 GiB because "various hardware units read past
   the end of an object").

Layout chosen (patch 1, `anv_private.h` `VA32_*`):

| range | use | size |
|---|---|---|
| 2 MiB - 64 MiB | general state pool | 62 MiB |
| 64 - 256 MiB | dynamic state pool | 192 MiB |
| 256 - 384 MiB | binding table pool | 128 MiB (2048 blocks of 64 KiB) |
| 384 - 640 MiB | surface state pool | 256 MiB |
| 640 - 768 MiB | instruction state pool | 128 MiB |
| 768 MiB - 1 GiB | client-visible heap (BDA capture/replay) | 256 MiB |
| 1 GiB - 4 GiB - 64 MiB | heap for everything else (memory objects, batches, scratch, imports) | 2.94 GiB |

Mechanics: the layout is a runtime struct (`anv_va_layout` in the physical device, filled by
`anv_physical_device_init_va()`: 48-bit values when `supports_48bit_addresses`, the table above otherwise, and the 48-bit
values in relocation mode where only the pool start offsets matter), used by the pool/VMA initialisation, `STATE_BASE_ADDRESS`,
the batch decoder and the shader constant-data address. The state pools get a `max_size` (`anv_block_pool`): growth past
the range returns `VK_ERROR_OUT_OF_DEVICE_MEMORY` instead of overlapping the next pool (the 48-bit layout never needed this,
its pools grow into 1 GiB gaps). The "high" VMA heap is empty in the 32-bit layout (`anv_vma_alloc` skips it) and the
"low" heap is the single 2.94 GiB heap; `ANV_BO_ALLOC_32BIT_ADDRESS` (scratch, state pools in relocation mode) keeps working
because everything is 32-bit. The Vulkan memory heap is sized from the address space (2.94 GiB - 256 MiB = **2.69 GiB**,
reported by DXVK as `Heap 0: 2.68 GiB`) instead of the 2 GiB clamp that relocation mode on CHV gets today (the "GTT size
larger than 2 GiB but not support for 48-bit addresses" warning at every device creation is gone with it). `use_relocations`
becomes `ver < 8` plus an escape hatch `HASVK_USE_RELOCATIONS=1` (verified: gives the old 1.2 driver, `r5/vkdepth` correct).

Not changed: `supports_48bit_addresses` stays false (no 48B flag on the BOs, the kernel's `PIN_ZONE_4G` check is a no-op in
a 4 GiB VM), canonical-address helpers are identities below 4 GiB, the gfx8 unit tests in `tests/` get the extra
`max_size` argument (0 = unlimited) and otherwise keep their 48-bit constants.

Diff size estimate before implementing: ~250 lines for the 32-bit layout. Actual: 234.

## 3. Patches (`mesa/vk13/`)

| patch | lines | what |
|---|---|---|
| `0001-hasvk-32-bit-softpin-layout-for-Cherry-Trail-4-GiB-P.patch` | +224/-49 (12 of them in tests) | section 2; also keeps relocation mode available via `HASVK_USE_RELOCATIONS=1` |
| `0002-hasvk-fail-instead-of-spinning-when-a-state-pool-is-.patch` | +62/-8 | block/state pool exhaustion returns a failure (`ANV_STATE_NULL`) instead of spinning in `anv_block_pool_alloc_new()`; the binding-table block path turns it into `VK_ERROR_OUT_OF_DEVICE_MEMORY`; a command buffer's first binding-table block is allocated at `vkBeginCommandBuffer` instead of at allocation (section 6) |
| `0003-hasvk-256-byte-push-constants-and-VK_KHR_pipeline_li.patch` | +8/-1 | `MAX_PUSH_CONSTANTS_SIZE 128 -> 256` (anv's value; Vulkan 1.4 minimum, DXVK >= 2.7 requirement), expose `VK_KHR_pipeline_library` (DXVK >= 2.7 requires it; it only adds a struct, hasvk has no graphics pipeline libraries so it is never fed one) |
| `0004-hasvk-option-to-report-the-descriptor-indexing-featu.patch` | +38/-18 | driconf/env option `hasvk_report_descriptor_indexing` (default off): reports the Vulkan 1.2 descriptor-indexing feature group; update-after-bind bindings whose descriptor type cannot be bindless on gfx8 (surfaces) fall back to the binding table instead of asserting. Section 7 |

Build: applied on top of 0001-0006 (`mesa/PKGBUILD` order; they only touch `src/intel/vulkan_hasvk/`). Built in the
leftover devtools chroot `/var/lib/archbuild/gpd-x86_64/r7` (Mesa 26.2.4 tree + `build/` from R7, same meson options as
`mesa/PKGBUILD`, plus a `build-dbg/` with `-Db_ndebug=false` for the assert-enabled driver used in the CTS runs, see
`r9/build-dbg.sh`); only `ninja src/intel/vulkan_hasvk/libvulkan_intel_hasvk.so` is needed after a hasvk change (~1 min).
Test prefix on the GPD: `~/vk13-test/{lib,icd,deqp,r5,dxvk271}` with ICD json files per build (`r9/mkicd.sh`) and
`VK_DRIVER_FILES=~/vk13-test/icd/vk13final.json`.

## 4. Tests [M]

### 4.1 Feature/property dumps
`r5/vkq` (installed driver -> patched; `logs/vkq-stock.txt` vs `vkq-vk13.txt` on the GPD): `api 1.2.354 conformance
1.2.0.0` -> `api 1.3.354 conformance 1.3.0.0`; `bufferDeviceAddress 0 -> 1`, `bufferDeviceAddressCaptureReplay 0 -> 1`;
127 -> 129 device extensions (`VK_KHR_buffer_device_address`, `VK_EXT_buffer_device_address`); with patch 3 also
`VK_KHR_pipeline_library` and `maxPushConstantsSize = 256` (`vulkaninfo`); with patch 4's option `descriptorIndexing`,
`runtimeDescriptorArray`, `descriptorBindingSampledImageUpdateAfterBind`, `PartiallyBound` etc. = 1. Everything else in the
dump is identical. The "kernel reported a GTT size larger than 2 GiB but not support for 48-bit addresses" warning at
device creation is gone (it was the relocation-mode heap clamp). `HASVK_USE_RELOCATIONS=1` reproduces the stock dump.

### 4.2 dEQP-VK (CTS of VK-GL-CTS opengl-es-cts-3.2.14.1, `vk-default` mustpass, headless)
`r9/deqprun.sh` (crash/abort resume, per-test results), `runall2.sh`/`queue2.sh`/`queue3.sh` (the plan), `compare.sh`
(per-test diff of every patched run against the stock run of the same list). Builds: `vk13dbg` = patch 1 only (first
build, 64 MiB client-visible heap), `vk13p2dbg` = patches 1 + 3 (before patch 2, so the two `allocate_many` cases and
`object_management.max_concurrent.command_buffer_*` spin on it, section 6), `vk13finaldbg` = patches 1-4; `dbg` =
`b_ndebug=false` (asserts on). Stock = installed `1:26.2.4-1.7`.

Complete when this report was written:

| case list (count) | driver | result | wall |
|---|---|---|---|
| `binding_model.buffer_device_address.*` (4716) | vk13finaldbg | **4716 Pass** | 399 s |
| same | vk13dbg (first build) | 4704 Pass, 3 ResourceError (`capture_replay_stress.seed_0-2`: OUT_OF_DEVICE_MEMORY from the 64 MiB client-visible heap, then dEQP aborted the run; fixed: 256 MiB), 9 not run | |
| `api.info.*` + `info.*` (12661) | vk13p2 | 6898 Pass, 5763 NotSupported, 0 Fail (dEQP totals) | |
| `api.buffer*` (4476) | vk13p2dbg | 1415 Pass, 3061 NotSupported, 0 Fail (dEQP totals) | |
| `api.command_buffers.*` (127) | vk13finaldbg vs stock | 57 Pass + 70 NotSupported, **identical per test** (incl. `allocate_many_primary/secondary`, which spun before patch 2) | 5 s vs 4 s |
| `pipeline.monolithic.push_constant.*` + `push_descriptor.*` (133) | vk13p2dbg vs stock | 133 Pass vs 131 Pass + 2 NotSupported: only `push_constant.graphics_pipeline.range_size_256[_command2]` differ (NotSupported on stock, Pass with patch 3) | |

The per-test result files of the `info`, `apibuf` and `push` runs were produced by an earlier version of the runner whose
parser was broken; the numbers above are dEQP's own totals from the run output (`logs/*.out`), re-parsed with
`r9/reparse.sh`.

Sampled groups finished later (patches 1+3 build `vk13p2dbg` vs stock, same lists; from `compare.sh`):

| list (count) | stock | vk13p2dbg | per test |
|---|---|---|---|
| `memory.*` /5 (1254) | 537 Pass, 717 NS | same | identical |
| `compute.*` /50 (1216) | 91 Pass, 1125 NS | same | identical |
| `api.copy_and_blit` /200 (1299) | 343 Pass, 956 NS | same | identical |
| `query_pool` /10 (1825) | 1801 Pass, 24 NS | same | identical |
| `synchronization` /60 (1080) | 343 Pass, 737 NS | same | identical |
| `dynamic_state` /10 (67) | 12 Pass, 55 NS | same | identical |
| `pipeline.monolithic` /200 (1490) | 788 Pass, 701 NS, **1 Fail** (`interface_matching...float64.multiple_locations.vec2`) | 789 Pass, 701 NS, 0 Fail | the stock failure passes with the patches (not investigated: may be nondeterministic) |
| `glsl` /50 (478) | 356 Pass, 122 NS | 359 Pass, 119 NS | 3 cases NotSupported on stock pass (they need features the patches add) |
| `draw` /25 (1229) | 932 Pass, 292 NS, **5 Fail** | 930 Pass, 292 NS, **7 Fail** | all failures on both drivers are `indexed_draw`/`indirect_draw` cases with `offset_minus_one` / `offset_negative_large` / `alloc_offset` (index buffer offsets outside the buffer, robustness of out-of-bounds index fetches), and the failing *set* differs between the two runs: nondeterministic on this hardware/driver, present before the patches, not a softpin regression as far as these runs show. Worth a repeat run on both drivers when the device is free. |
| `robustness` /60 (1608) | (queue4) | 704 Pass, 904 NS, 0 Fail | |
| `draw` /25 (1229), final build | 932 Pass, 5 Fail | 929 Pass, 292 NS, 8 Fail | same nondeterministic index-offset cases |
| `pipeline.monolithic` /200, final build | 788 Pass, 1 Fail | 789 Pass, 701 NS, 0 Fail | |
| `push_constant`/`push_descriptor` (133), final build | 131 Pass, 2 NS | 133 Pass | |

Draw-group failures, repeated (`lists-drawfail.txt` = the 10 cases that failed in either run, twice per driver;
`lists-indexed.txt` = every 3rd `indexed_draw` case of renderpass + dynamic_rendering primary): stock **5/10 and 5/10
fail**, patched 2/10 and 4/10 (different cases each time); indexed sample: stock 16/121 fail, patched 13/121. All are
`offset_minus_one` / `offset_negative_large` / `alloc_offset` / `indexed_alloc_offset_16` cases (index fetches outside the
bound range). **Pre-existing and nondeterministic on this hardware/driver, not a softpin regression.**

**Final CTS state (all queues finished 2026-10-12 ~17:00, `compare.sh`):** per test identical to stock: memory,
compute, copy_and_blit, query_pool, synchronization, dynamic_state, robustness, cmdbuf (final), devinit (final, 688),
`binding_model.shader_access` sample (864), `binding_model` others (548), texture sample (140). Differing groups differ
only as described: push (256-byte cases NotSupported -> Pass), glsl (+3 Pass), pipeline (stock's 1 Fail passes),
ssbo sample (641 Pass / 363 NS, 0 Fail: BDA variants NotSupported on stock now pass), draw/indexed (the nondeterministic
index-offset failures above). Non-pass results on the patched driver apart from those draw cases: the 3
`capture_replay_stress` ResourceErrors of the first debug build (fixed, 4716/4716 on the final build), 2
`object_management.alloc_callback_fail` QualityWarnings (stock identical), and in `descriptor_indexing.*` with patch 4's
claim **6 Pass, 1 NotSupported (uniform_buffer), 1 Fail: `storage_texel_buffer`**. That fail is the known limit of
patch 4 (update-after-bind for non-sampler descriptors falls back to the binding table, so a later update is not seen);
DXVK only uses the sampler path. **0 GPU hangs and no asserts in the whole session.**

### 4.3 DXVK 2.7.1 (own build, `/var/lib/archbuild/gpd-dxvk/root/build-271`, x64 + x32 DLLs in `~/vk13-test/dxvk271`)
`r9/dxvktest.sh`: `r5/d3d9tex.exe` (64-bit, Wine 11.19, `~/.wine`) with DXVK's `d3d9.dll` next to the exe
(`WINEDLLOVERRIDES=d3d9=n`, prefix untouched), `DXVK_LOG_LEVEL=info`:

| driver | result |
|---|---|
| patches 1-3 (`vk13p2`) | DXVK: `Found device: Intel(R) HD Graphics (CHV)` ... `Skipping: Device does not support required feature 'descriptorIndexing'` -> no adapter |
| patches 1-4 + `hasvk_report_descriptor_indexing=true` (`vk13p3dbg`, then final `vk13finaldbg`) | device created (`Heap 0: 2.68 GiB`, 2 memory types), 300 and 600 frames rendered, readback: background (0,0,255), textured red (255,0,0) and white (255,255,255) squares, untextured green (0,255,0): **correct**; 0 GPU hangs, no assert |

Not tested: 32-bit DXVK (no lib32 build of the patches), D3D11, a real game (screen off).

### 4.4 Relocation-mode escape hatch
`HASVK_USE_RELOCATIONS=1` with the final build: `vkq` = stock dump (1.2.354), `r5/vkdepth` correct (11.40 ms, same as
softpin in the same minute).

## 5. Performance [M]

`r5/vkb` (headless, one command buffer with 64 `vkCmdCopyBuffer` over 32 buffers, 2000 submits, fence wait every 8;
same numbers as R5 2c for the stock driver):

| driver | per submit wall / utime / stime |
|---|---|
| stock 1.7 (relocations) | 734.7 / 115.0 / 205.2 us |
| vk13 (softpin) | 731.0 / **71.6** / **179.1** us |

Wall is GPU-bound and unchanged; the CPU side loses the per-submit surface-state relocation + clflush work that R5 measured
at ~65 % of hasvk's user time (-38 % user, -13 % sys here). `r5/vkdepth` D16 (GPU fill-rate bound, HiZ): stock 12.05
ms/frame, vk13 12.24 ms/frame (run-to-run spread ~0.5 ms; a later pair gave 11.40 for both modes), results correct in
every run. dEQP group wall times are in the section 4 table where both drivers ran the same list.

Not measured: on-screen games (lid closed, screen off, as required), 32-bit Wine (no lib32 build of the patches).

## 6. Risks, and what would be needed to ship

Found and fixed during testing:
- **Pool exhaustion spins forever** (patch 2). `anv_block_pool_alloc_new()` loops until the pool has grown; a failed grow
  (which the bounded 32-bit pools can produce, and which a BO allocation failure could always produce on Broadwell too)
  spun at 100 % CPU: `dEQP-VK.api.command_buffers.allocate_many_primary` (10000 command buffers x 64 KiB binding-table
  block each > 128 MiB pool) hung the first build. Now the allocators return a failure (`ANV_STATE_NULL`) that the
  binding-table path turns into `VK_ERROR_OUT_OF_DEVICE_MEMORY`, and a command buffer takes its first block at
  `vkBeginCommandBuffer`, not at allocation. Callers of the other state pools (surface states, dynamic state, shaders)
  still assume success: exhausting 256 MiB of surface states / 192 MiB of dynamic state / 128 MiB of shader code now
  crashes on a NULL map instead of hanging. [H] A game reaching that is unlikely (4 M surface states; DXVK keeps a
  handful of command buffers).
- **Client-visible heap** 64 MiB was too small for `capture_replay_stress` (100 buffers of up to 4 MiB): 256 MiB.

Residual risks:
1. **Address-space budget.** Everything a process uses on the GPU must fit in 4 GiB per `VkDevice`: 2.69 GiB Vulkan heap
   (device memory objects) + ~250 MiB for batches/scratch/imports + 1 GiB of state pools. Relocation mode let the kernel
   place objects anywhere and allowed a 2 GiB heap, so for memory objects this is a gain, but the fixed pool sizes are
   new hard limits (section 2 table). [H] Plenty for everything this device runs; the symptom would be
   `VK_ERROR_OUT_OF_DEVICE_MEMORY` (binding tables, memory) or a crash (other pools), never a GPU hang.
2. **Every memory object is in every execbuf** with softpin (`anv_batch_chain.c`, also on Broadwell): R5 measured
   ~0.65 us per object per submit. A game with 500 live `VkDeviceMemory` objects pays ~0.3 ms per submit. DXVK
   allocates in 16-64 MiB chunks (tens of objects), SDL a few. Not measured on a game here (no on-screen runs).
3. **Snooped state pools.** With softpin the pool BOs are allocated `ANV_BO_ALLOC_SNOOPED` (cached + snooped on this
   non-LLC part) like Broadwell's; in relocation mode they are userptr memfd mappings, which are snooped too, so no change
   in kind. R4's DSI display flashes were triggered by large *uploads* through snooped staging textures; state pools are
   small writes; not observable this session (lid closed), an on-screen check is listed in section 8.
4. **CTS coverage.** ~22 k dEQP-VK cases plus the sampled groups (all finished, section 4.2), in the groups that softpin/BDA touch (binding model, API, memory,
   command buffers, draw, pipeline, compute, glsl, synchronization, queries, copies, robustness, push constants),
   sampled, all matching the installed driver. The full mustpass is 1.69 M cases (days on this Atom); a conformance
   claim for CHV would need all of it plus the WSI groups on-screen.
5. **Nothing CHV-specific showed up** in the Broadwell softpin paths (secondary command buffer calls, chaining, A64
   messages, binding-table pool with per-block `STATE_BASE_ADDRESS`): 0 GPU hangs, 0 asserts in the `b_ndebug=false`
   build across all runs.

To ship: add patches 1-3 to `mesa/PKGBUILD` (and `mesa/lib32/PKGBUILD` so 32-bit Wine games get the same driver;
untested here), rebuild with `mesa/build.sh` (+ `lib32`), install with `mesa/install.sh`, then check the Vulkan users of
this device on screen: SDL `vulkan`/`gpu` renderer games, the DXVK games (`~/Games/tools/dxvk/gpd/`, DXVK 2.4.1 - it
does not use BDA, so it mostly gains the cheaper submits), MangoHud's Vulkan layer. Keep `HASVK_USE_RELOCATIONS=1` as the
per-game escape hatch. Patch 4 only matters with a DXVK >= 2.7 build.

## 7. The DXVK >= 2.7 stretch goal

DXVK 2.7.1's hard requirements beyond Vulkan 1.3 [S] (`src/dxvk/dxvk_device_info.cpp` `getFeatureList()`, `require =
true` entries, and `checkDeviceCompatibility()`): `bufferDeviceAddress`, `descriptorIndexing`,
`descriptorBindingSampledImageUpdateAfterBind`, `descriptorBindingUpdateUnusedWhilePending`,
`descriptorBindingPartiallyBound`, `runtimeDescriptorArray`, `shaderInt64`, `samplerMirrorClampToEdge`,
`uniformBufferStandardLayout`, `hostQueryReset`, `timelineSemaphore`, `vulkanMemoryModel`, `shaderDrawParameters`,
`maintenance4`, `maintenance5`, `synchronization2`, `dynamicRendering`, `shaderDemoteToHelperInvocation`,
`shaderZeroInitializeWorkgroupMemory`, `robustBufferAccess2` + `nullDescriptor` (`EXT_robustness2`),
`VK_KHR_pipeline_library`, `VK_KHR_swapchain`, and `maxPushConstantsSize >= 256`. Of these, hasvk 26.2.4 on CHV lacked
BDA (patch 1), 256-byte push constants and `KHR_pipeline_library` (patch 3), and the descriptor indexing group
(patch 4); `maintenance5`/`6`, `robustness2`, `shaderInt64` etc. are already exposed by 26.2.4.

### 7.1 Push constants and pipeline library (patch 3, +8/-1) [S][M]
`MAX_PUSH_CONSTANTS_SIZE` is only the size of the client data array inside `anv_push_constants`, which hasvk pushes
whole (plus dynamic offsets etc.); anv has used 256 since Vulkan 1.4. The per-stage push space on gfx8 is 2-4 KiB
(`max_constant_urb_size_kb` split over the active stages) and the compiler's push limit is 64 registers (32 for vec4
stages): 256 + ~100 bytes = 12 registers, so pushed UBO ranges still fit. dEQP `pipeline.monolithic.push_constant.*`
133/133 pass (`range_size_256` + `_command2` were NotSupported on stock). `VK_KHR_pipeline_library` only adds
`VkPipelineLibraryCreateInfoKHR`; without `EXT_graphics_pipeline_library` nothing can hand it to hasvk.

### 7.2 Descriptor indexing: what DXVK uses, what gfx8 can do [S][M]
DXVK 2.7.1 uses the descriptor indexing features in exactly one place: `DxvkSamplerDescriptorHeap`
(`dxvk_sampler.cpp:195-260`): one descriptor set with a single `VK_DESCRIPTOR_TYPE_SAMPLER` binding of
`MaxSamplerCount = 2048`, flags `UPDATE_AFTER_BIND | UPDATE_UNUSED_WHILE_PENDING | PARTIALLY_BOUND`, pool
`UPDATE_AFTER_BIND`, and shaders index it as a runtime array (`dxbc_compiler.cpp:7854`, `d3d9_fixed_function.cpp:423`).
Images, buffers and texel buffers stay in ordinary per-pipeline sets (no update-after-bind, no `NonUniform`).

hasvk on gfx8 already has bindless *samplers* (`has_bindless_samplers = ver >= 8`: the sampler state pointer travels in
the message header, the descriptor set stores the handle; `anv_descriptor_requires_bindless()` routes update-after-bind
sampler bindings there, `anv_nir_apply_pipeline_layout.c:1377`). Bindless *surfaces* need `BINDLESS_SURFACE_STATE_BASE_ADDRESS`
(gfx9+); on gfx8 every image/buffer descriptor is a binding-table entry written when the draw is recorded, and a stage
has at most 240 of them (`MAX_BINDING_TABLE_SIZE`). That is why anv never exposed `descriptorIndexing` on Broadwell.

Patch 4 therefore adds an opt-in claim, `hasvk_report_descriptor_indexing` (driconf option, also settable as an
environment variable like `hasvk_report_vk_1_3_version`): the feature group is reported, sampler update-after-bind is
real (bindless), update-after-bind bindings of other types fall back to the binding table (their descriptors are read at
record time, so a later update is not seen; previously they would have hit an assert). Measured [M]: DXVK 2.7.1
(64-bit, own build from the gpd-dxvk chroot) with the option: `d3d9tex.exe` creates the device (`Heap 0: 2.68 GiB`),
renders 300 and 600 frames, readback correct (blue background, red + white textured squares, green untextured quad), on
the assert-enabled driver, no GPU hang; without the option it logs `Device does not support required feature
'descriptorIndexing'` and refuses the adapter. dEQP `descriptor_indexing.*` with the option: section 4.

### 7.3 Estimate for a real (non-conformant but honest) implementation [H]
What could be made real on gfx8, so that the option becomes a default:
- update-after-bind for sampled/storage images and texel buffers: give each such descriptor a 64-byte surface state
  *copy* owned by the descriptor set (allocated from the surface state pool like the per-set buffer-view states already
  are), fill it at `vkUpdateDescriptorSets` from the view's state (per plane, per layout), point the binding table at
  the copy, zero-initialise with a null surface state for `partiallyBound`. Changes in `anv_descriptor_set.c`
  (layout sizes, set allocation, write/copy paths), `genX_cmd_buffer.c:emit_binding_table`, `anv_private.h`:
  ~250-400 lines. In the 32-bit layout the copies could even live in the descriptor pool BO (every address is within
  4 GiB of Surface State Base Address, which hasvk sets to the binding-table block, i.e. below 384 MiB);
- `variableDescriptorCount`, runtime arrays, non-uniform indexing: largely present (`nir_lower_non_uniform_access`
  waterfall loops in `anv_pipeline.c:527`, variable count in the layout code); needs the CTS run to find the gaps:
  ~50-100 lines;
- limits: the spec minimums (500000 update-after-bind descriptors per stage, `maxPerStageUpdateAfterBindResources`)
  cannot be met with 240 binding-table slots; the honest values (~230) make `descriptorIndexing = true` non-conformant
  by definition. So the result is a driver that runs DXVK >= 2.7 and most descriptor-indexing software correctly, but
  cannot claim the feature conformantly. Total: 300-500 lines, a CTS pass over `dEQP-VK.descriptor_indexing.*` and
  `binding_model.*` (~200 k cases with the update-after-bind variants), estimated 2-3 days. Not done here.

## 8. Open points / not tested

- Not run on screen (lid closed): no game, no WSI/swapchain test, no check for DSI display flashes with the snooped
  state pools. First on-screen checks when installing: an SDL `vulkan` renderer game, a DXVK game, MangoHud.
- 32-bit: no lib32 build of the patches, so 32-bit Wine/DXVK (what the installed games use) is untested. The address
  budget is per-process on the GPU side and unaffected by the process' pointer size.
- CTS: sampled, not complete (section 4); descriptor indexing only with the opt-in claim and only DXVK's usage pattern
  plus the `dEQP-VK.descriptor_indexing.*` group.
- The `conformanceVersion = 1.3.0.0` string comes from upstream's `use_softpin ? 3 : 2`; strictly it should stay 1.2
  (or 0.0) until a CTS submission, which will not happen for a one-device patch. Cosmetic; `vulkaninfo` shows it.
- Multi-threaded pool exhaustion: a thread that observed "pool full" just before the failing thread rolls back may
  wait on the futex until the next successful growth (patch 2). Only reachable after a real exhaustion.
- Upstream: the 32-bit layout is a clean addition and would probably be accepted for hasvk (CHV is the only gfx8
  without 48-bit addresses; hasvk is in maintenance mode but takes fixes). Draft subject: "hasvk: use softpin with a
  32-bit address layout on Cherry Trail". Not sent (not in scope).
- Cleanup: `~/vk13-test` on the GPD (560 MB: driver builds, deqp-vk + data, logs), the r7 chroot's `build-dbg`
  (desktop), DXVK 2.7.1 in `/var/lib/archbuild/gpd-dxvk/root/build-271`, the scratch Mesa tree branch `vk13`.

## 9. Follow-up: 32-bit (Dark Souls), DXVK 2.7.1 and the readback workaround

### 9.1 How to test Dark Souls on screen (Robin)
```
~/vk13-test/darksouls-test.sh stock            # today's launcher settings, baseline (expect 30 fps with the cap)
~/vk13-test/darksouls-test.sh vk13-24          # same DXVK 2.4.1 DLL, patched 32-bit driver (softpin, real 1.3)
~/vk13-test/darksouls-test.sh vk13-24-reloc    # patched driver in relocation mode (isolates the softpin part)
ASYNC=0 ~/vk13-test/darksouls-test.sh vk13-24  # async readback off: expect ~19-24 fps, GPU ~75 % (R5/dxvk README)
~/vk13-test/darksouls-test.sh vk13-27          # DXVK 2.7.1-gpd + gplasync + readback port, patched driver (enabled, 9.3)
```
Run from a terminal in sway (foot) on the GPD, never two at once. The script prints the variant, driver and DLL
(sha256) at start, swaps `DATA/d3d9.dll` only for `vk13-27` and restores it on any exit (verified by sha256; dry-run
tested incl. `kill -TERM`), applies shelf's Dark Souls settings (cpu/gpu turbo on, 8 W, GPU pin, MangoHud 30 fps cap)
through game-wrap like the real launcher, and logs DXVK (`DXVK_LOG_LEVEL=info`, with the GPD wait log) to
`~/vk13-test/logs/ds-VARIANT-TIME/` and wine's stderr to `ds-VARIANT-TIME.log`. What to look for: fps (MangoHud,
Ctrl+Shift+F12 if hidden) in the same spot for each variant; black screen or missing textures (the R5 2.6.x symptom,
appears after a few hundred frames); a frozen picture = GPU hang (the script prints the kernel's hang count before and
after; `journalctl -k | grep -Ei 'gpu hang|Resetting rcs'`); with `ASYNC=0` the stall is back (CPU and GPU take
turns). Bail out: quit the game, or `pkill -x DARKSOULS.exe` from another terminal (the script then restores the
DLL); `pkill -f darksouls-test` also restores. Nothing else on the device is changed by the script.

### 9.2 Does DXVK 2.7.1 still need the async-readback patch? [S]
Yes. In 2.7.1 `D3D9DeviceEx::LockImage()` (`src/d3d9/d3d9_device.cpp:5058`) still ends the readback path with
`WaitForResource(*mappedBuffer, seq, Flags)` ("Wait until the buffer is idle which may include the copy (and resolve)
we just issued"): a `READONLY` lock of a SYSTEMMEM surface that `GetRenderTargetData` was copied into waits until the
GPU has executed that copy, i.e. the whole frame before it. Same logic as 2.4.1; what changed around it: the staging
throttle is now `WaitStagingBuffer()` (`waitForFence` on a staging fence instead of `waitForResource(marker)`),
`WaitForResource` takes a `DxvkPagedResource&`, and the `D3DDEVINFO_RESOURCEMANAGER` mingw workaround of our patch is
upstream. Port: `dxvk/vk13/0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch` (153 lines; same env switch
`DXVK_GPD_ASYNC_READBACK=1`, same wait log labels, `StagingThrottle` now times the fence wait). Built with
`dxvk-gplasync-2.7.1-1.patch` (upstream gitlab.com/Ph42oN/dxvk-gplasync `patches/`, sha256
`848956793ada245a94a14b930f1d24b5545d7214255df90d02ef2eee831280b9`, identical to `dxvk-gplasync-2.7-1.patch`) in the
gpd-dxvk chroot (`/var/lib/archbuild/gpd-dxvk/root/build-271gpd`, `-Denable_d3d8=false -Denable_d3d10=false`,
stripped): `~/vk13-test/dxvk271gpd/{x32,x64}/d3d9.dll` on the GPD (x32 sha256 d78f8a5f…). Measured effect: 9.3.

### 9.3 32-bit results
Setup: `r9/dxvk32test.sh` (32-bit Wine 11.19 old-style from `~/Games/tools/wine32`, prefix `~/.wine32`, DLL next to the
exe with `d3d9=n`, claim env vars set, `DISPLAY=:0` so Wine has a window driver; the GPD's screen stayed off) with
`r5/d3d9tex32.exe` (2400 frames, readback of the last frame) and `r9/d3d9rb32.exe` (Dark Souls pattern: K=20 textured
quads + 16x16 R32F luminance target → `GetRenderTargetData` → READONLY lock every frame, colour/texture check every 100
frames, 600 frames); `r9/run32matrix.sh` runs the matrix DLL x driver. Drivers: `stock` = installed lib32 hasvk 1.7
(relocations), `vk13reloc` = patched lib32 build with `HASVK_USE_RELOCATIONS=1`, `vk13` = patched lib32 build (softpin).
The patched lib32 driver loads in 32-bit Wine with the 32-bit softpin layout (DXVK logs `Memory Heap[0]: 2752 MiB`).

Complete matrix (`~/vk13-test/logs/matrix32-163949.txt` on the GPD; d3d9rb32: 600 frames, K=20, ms/frame median,
"lock" = time in the readback lock per frame):

| DLL (x32) | driver | d3d9tex32 2400 frames | d3d9rb32 async=0 | d3d9rb32 async=1 |
|---|---|---|---|---|
| 2.4.1-gpd (the game's) | stock | correct | 6.08 ms, lock 5.13 | 3.63 ms, lock 0.03 |
| 2.6.2 | stock | **correct** (R5 saw textures lost after ~300 frames with this DLL on driver 1.3/1.4; today's 1.7 has the HiZ patch, and this run is headless) | - | - |
| 2.7.1-gpd | stock | n/a: `No adapters found` (no 1.3/BDA on the stock driver) | n/a | n/a |
| 2.4.1-gpd | vk13reloc | correct | 6.04 ms, lock 5.19 | 3.65 ms, lock 0.03 |
| 2.6.2 | vk13reloc | correct | - | - |
| 2.7.1-gpd | vk13reloc | n/a: `No adapters found` (relocation mode has no BDA, by design) | n/a | n/a |
| 2.4.1-gpd | vk13 (softpin) | correct | 6.22 ms, lock 5.27 | 3.60 ms, lock 0.03 |
| 2.6.2 | vk13 | correct | - | - |
| 2.7.1-gpd | vk13 | correct (Wine printed `wine client error:11c: write: Bad file descriptor` at exit, rc 0) | **9.32 ms, lock 8.90** | **7.00 ms, lock 0.05** |

All rb runs: 0 bad checks; luminance mean 1.000 (async=0) / 0.997 (async=1, a frame late). **0 GPU hangs in the whole
matrix** (kernel count before/after each line). Conclusions:
- 32-bit DXVK 2.7.1 works on the patched driver (softpin) and only there; no hang, no lost textures in these tests.
- The async readback patch is needed on 2.7.1 too and works there (lock 8.90 -> 0.05 ms/frame).
- **2.7.1 is slower than 2.4.1 in this test**: 7.00 vs 3.60 ms/frame with async readback (9.32 vs 6.22 without). Not
  analysed; the test is CPU/submit-bound tiny draws, so it may or may not show in Dark Souls (GPU-bound at 30 fps). The
  on-screen kit (`vk13-27` vs `vk13-24`) answers that.
- Softpin vs relocations with 2.4.1: no difference beyond noise here.

The vk13-27 variant of the Dark Souls kit is **enabled** (`~/vk13-test/vk13-27-ok` created 2026-10-12 16:47 after
these lines passed).

