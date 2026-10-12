# DXVK 2.7.1 for the R9 Vulkan 1.3 experiment (not used by the installed games)

- `dxvk-gplasync-2.7.1-1.patch`: unchanged from https://gitlab.com/Ph42oN/dxvk-gplasync `patches/`
  (sha256 `848956793ada245a94a14b930f1d24b5545d7214255df90d02ef2eee831280b9`, byte-identical to its `dxvk-gplasync-2.7-1.patch`).
- `0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch`: port of `../0001-gpd-d3d9-async-readback-and-wait-log.patch`
  (2.4.1) to v2.7.1: same `DXVK_GPD_ASYNC_READBACK=1` switch and wait log; `WaitStagingBuffer()` (now a fence wait) is timed
  as `StagingThrottle`; the `D3DDEVINFO_RESOURCEMANAGER` mingw fix is upstream in 2.7.1. 2.7.1's `LockImage()` still waits
  for the GPU after `GetRenderTargetData` (`mesa/reports/R9-vulkan13.md` section 9.2), so the patch is still needed.
- DXVK >= 2.7 on hasvk needs the R9 Mesa patches `mesa/vk13/0001-0004` and `hasvk_report_descriptor_indexing=true`.

Build (desktop, 2026-10-12, in the `dxvk/build.sh` chroot `/var/lib/archbuild/gpd-dxvk`, copy `root/build-271gpd`):
```
git clone --branch v2.7.1 --depth 1 --recurse-submodules --shallow-submodules https://github.com/doitsujin/dxvk build-271gpd
cd build-271gpd && patch -Np1 < dxvk-gplasync-2.7.1-1.patch && patch -Np1 < 0001-gpd-d3d9-async-readback-and-wait-log-2.7.1.patch
meson setup b32 --cross-file build-win32.txt --buildtype release -Db_ndebug=true -Denable_d3d8=false -Denable_d3d10=false
ninja -C b32 src/d3d9/d3d9.dll && i686-w64-mingw32-strip b32/src/d3d9/d3d9.dll        # x32 (b64 likewise with build-win64.txt)
```
(mingw-w64-gcc 16.2, meson 1.12; `build-win32.txt`/`build-win64.txt` are DXVK's own cross files.) Result on the GPD:
`~/vk13-test/dxvk271gpd/{x32,x64}/d3d9.dll` (x32 sha256 d78f8a5f6a60b222...). Not in `dxvk/build.sh`; Robin decides.
