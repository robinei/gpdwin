# DXVK 2.6.2 build (2026-10-12): the last release before 2.7's binding-model change

- `dxvk-gplasync-2.6.2-1.patch`: unchanged from https://gitlab.com/Ph42oN/dxvk-gplasync `patches/`
  (sha256 `8d3ad87f958337c7cec614cec620a9430077c1532b9b14b04f2653e7a34687d7`).
- Async readback / wait log: `../vk13/0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch` applies to v2.6.2 unchanged
  (offsets only; the 2.4.1 version does not).

Build (desktop, `gpd-dxvk` chroot, copy `root/build-262`, x32 only):
```
git clone --branch v2.6.2 --depth 1 --recurse-submodules --shallow-submodules https://github.com/doitsujin/dxvk build-262
cd build-262 && patch -Np1 < dxvk-gplasync-2.6.2-1.patch && patch -Np1 < 0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch
meson setup b32 --cross-file build-win32.txt --buildtype release -Db_ndebug=true -Denable_d3d8=false -Denable_d3d10=false
ninja -C b32 src/d3d9/d3d9.dll && i686-w64-mingw32-strip b32/src/d3d9/d3d9.dll
```
Result: Dark Souls `dxvk-test/d3d9-gpd-2.6.2.dll` (sha256 `38105bfb80e4213d...`). Needs Vulkan 1.3 (Mesa 1.11 has it; no
descriptor indexing claim needed).

Headless 32-bit on Mesa 1.11 (`mesa/reports/r5/d3d9tex.c` 2400 frames, `r9/d3d9rb.c` 600 frames K=20):
| | d3d9tex32 | d3d9rb32 async=0 | d3d9rb32 async=1 |
|---|---|---|---|
| 2.4.1-gpd | correct | 6.20 ms/frame (lock 5.27) | 3.60 ms/frame |
| 2.6.2-gpd | correct | 9.19 ms/frame (lock 8.78) | 6.93 ms/frame |
| 2.7.1-gpd (R9, Mesa 1.9 test build) | correct | 9.32 ms/frame | 7.00 ms/frame |

So the slowdown in this CPU-bound micro test came between 2.4 and 2.6 (2.5's memory allocator rework is the likely
candidate, not measured), not with 2.7's binding model. 0 GPU hangs.
