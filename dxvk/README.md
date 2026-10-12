# DXVK for 32-bit Direct3D 9 games on the GPD (Dark Souls PTDE)

Why a build of our own (2026-10-12):
- 32-bit DXVK >= 2.5 is broken on hasvk here (2.5.x: GPU hang "CS error"; 2.6.x: textures lost after a few hundred
  frames); 2.0-2.4 work (`mesa/reports/R5-softpin-vulkan13.md`). We use 2.4.1 with the dxvk-gplasync patch
  (async pipeline compiles; `dxvk.enableAsync = true` in the game's dxvk.conf). DXVK 2.x needs the
  `hasvk_report_vk_1_3_version=true` driconf claim (env var in the launcher).
- Dark Souls stalled the CPU ~18 ms per frame: every frame it renders the scene luminance to a 16x16 R32F target,
  `GetRenderTargetData`s it into a SYSTEMMEM surface and locks it READONLY at once, so DXVK waits until the GPU
  has finished the whole frame (found with the wait log below: `LockImage ... a=16 b=16 c=114 d=0x20000010`,
  ~115 waits / 2.0 s per 5 s). CPU and GPU took turns instead of overlapping: 19-24 fps, GPU ~75% busy.

`0001-gpd-d3d9-async-readback-and-wait-log.patch` (d3d9 only):
- `DXVK_GPD_ASYNC_READBACK=1`: read-only locks of SYSTEMMEM images up to 64x64 pixels don't wait for the GPU;
  the game reads the last finished copy (eye adaptation a frame or two late). Dark Souls: **30 fps (its cap),
  GPU 93-96% busy**. Off unless the variable is set.
- Wait log: every GPU wait in the D3D9 lock/readback paths (`WaitForResource`, staging throttle) is timed; with
  `DXVK_LOG_LEVEL=warn` the first 40 waits over 1 ms are logged with the resource (w, h, format, pool<<28|flags or
  buffer size/usage/pool) and a 5 s summary per call type. Silent at the default log level.
- `d3d9_include.h`: newer mingw-w64 headers define `D3DDEVINFO_RESOURCEMANAGER`; put DXVK's own definition in
  `namespace dxvk` as upstream master does (2.4.1 no longer builds otherwise).

`dxvk-gplasync-2.4-1.patch`: https://gitlab.com/Ph42oN/dxvk-gplasync `patches/`, unchanged (sha256 `2f80b7d7...`).
`build.sh` builds `d3d9.dll` (x32) in an Arch chroot on the desktop (~2 min after the chroot exists).
