# hasvk Vulkan 1.3 patches for Cherry Trail (R9, 2026-10-12)

Not wired into `mesa/PKGBUILD` (the system driver); Dark Souls alone uses a lib32 build with them
(`~/Games/tools/mesa-vk13/`, `VK_DRIVER_FILES` in its launcher, docs/steam-library.md); see `mesa/reports/R9-vulkan13.md` for the design, test results and risks.
They apply on top of Mesa 26.2.4 + `mesa/0001-0006` (they only touch `src/intel/vulkan_hasvk/`).

| patch | what |
|---|---|
| `0001-hasvk-32-bit-softpin-layout-...` | softpin with a 32-bit address layout on CHV (4 GiB PPGTT): buffer device address, real Vulkan 1.3. `HASVK_USE_RELOCATIONS=1` gives the old relocation-mode driver (1.2). |
| `0002-hasvk-fail-instead-of-spinning-...` | state pool exhaustion returns `VK_ERROR_OUT_OF_DEVICE_MEMORY` instead of spinning; binding table blocks allocated at `vkBeginCommandBuffer` instead of at allocation. Needed with 0001's bounded pools. |
| `0003-hasvk-256-byte-push-constants-...` | `maxPushConstantsSize` 128 -> 256, `VK_KHR_pipeline_library`. Both required by DXVK >= 2.7 (and 256 B by Vulkan 1.4). |
| `0004-hasvk-option-to-report-the-descriptor-indexing-...` | opt-in `hasvk_report_descriptor_indexing=true` (driconf or environment): reports the descriptor indexing features DXVK >= 2.7 requires. Not conformant (gfx8 has no bindless surfaces); DXVK only needs the update-after-bind sampler heap, which works. |

To ship 0001-0003: add them to `source=()`/sums in `mesa/PKGBUILD` (and `mesa/lib32/PKGBUILD` for 32-bit Wine) and rebuild
with `mesa/build.sh` (+ `lib32`). 0004 is a per-game switch (`hasvk_report_descriptor_indexing=true` in the game's
environment, or a `<application executable="...">` entry in drirc), only useful together with a DXVK >= 2.7 build.
