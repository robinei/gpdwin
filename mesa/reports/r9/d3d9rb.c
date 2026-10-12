/* d3d9rb: the Dark Souls readback pattern for DXVK on hasvk (mesa/reports/R9-vulkan13.md, 32-bit section).
 * Every frame: draw K textured quads + one untextured quad into the back buffer (GPU work), render one textured
 * quad into a 16x16 R32F render target, GetRenderTargetData it into a SYSTEMMEM surface and LockRect(READONLY)
 * it at once (the eye-adaptation luminance readback that stalls the CPU on the GPU in Dark Souls), Present.
 * Every CHECK frames the back buffer is read back and the textured/untextured pixels are verified (the R5
 * "textures lost after a few hundred frames" symptom of 32-bit DXVK 2.6.x), and the 16x16 value is checked.
 * Prints ms/frame, the time spent in the readback lock, and the number of bad checks (exit code 1 if any).
 * Build (desktop): zig cc -target x86-windows-gnu -O1 -o d3d9rb32.exe d3d9rb.c -ld3d9 -lgdi32 -luser32
 *                  zig cc -target x86_64-windows-gnu -O1 -o d3d9rb.exe d3d9rb.c -ld3d9 -lgdi32 -luser32
 * Run: wine d3d9rb32.exe [frames] [K quads per frame] [CHECK]   (DXVK d3d9.dll next to it, WINEDLLOVERRIDES=d3d9=n) */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

#define W 640
#define H 360

struct tv { float x, y, z, rhw, u, v; };
struct cv { float x, y, z, rhw; DWORD c; };

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static int check_pixel(IDirect3DSurface9 *s, int x, int y, DWORD want, const char *what, int frame)
{
    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DSurface9_LockRect(s, &lr, NULL, D3DLOCK_READONLY))) { printf("frame %d: %s lock failed\n", frame, what); return 1; }
    DWORD c = ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] & 0xffffff;
    IDirect3DSurface9_UnlockRect(s);
    if (c != want) { printf("frame %d: %s at (%d,%d) = %06lx, want %06lx\n", frame, what, x, y, c, want); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 600;
    int K = argc > 2 ? atoi(argv[2]) : 20;
    int CHECK = argc > 3 ? atoi(argv[3]) : 100;
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d9rb";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3d9rb", "d3d9rb", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, W, H, NULL, NULL, wc.hInstance, NULL);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { printf("Direct3DCreate9 failed\n"); return 2; }
    D3DPRESENT_PARAMETERS pp = { 0 };
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *dev;
    HRESULT hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                         D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { printf("CreateDevice failed %08lx\n", hr); return 2; }

    IDirect3DTexture9 *tex;
    IDirect3DDevice9_CreateTexture(dev, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    D3DLOCKED_RECT lr;
    IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = ((x / 8 + y / 8) & 1) ? 0xffffffff : 0xffff0000;
    IDirect3DTexture9_UnlockRect(tex, 0);

    /* the 16x16 R32F luminance target + its SYSTEMMEM readback surface (Dark Souls: 16x16 R32F) */
    IDirect3DSurface9 *lum, *lumsys, *bb, *bbsys;
    hr = IDirect3DDevice9_CreateRenderTarget(dev, 16, 16, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &lum, NULL);
    if (FAILED(hr)) { printf("CreateRenderTarget R32F failed %08lx\n", hr); return 2; }
    IDirect3DDevice9_CreateOffscreenPlainSurface(dev, 16, 16, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &lumsys, NULL);
    IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    IDirect3DDevice9_CreateOffscreenPlainSurface(dev, W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &bbsys, NULL);

    struct tv tq[4] = { { 40, 40, 0, 1, 0, 0 }, { 300, 40, 0, 1, 1, 0 }, { 40, 320, 0, 1, 0, 1 }, { 300, 320, 0, 1, 1, 1 } };
    struct tv lq[4] = { { 0, 0, 0, 1, 0, 0 }, { 16, 0, 0, 1, 1, 0 }, { 0, 16, 0, 1, 0, 1 }, { 16, 16, 0, 1, 1, 1 } };
    struct cv cq[4] = { { 340, 40, 0, 1, 0xff00ff00 }, { 600, 40, 0, 1, 0xff00ff00 },
                        { 340, 320, 0, 1, 0xff00ff00 }, { 600, 320, 0, 1, 0xff00ff00 } };
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

    int bad = 0;
    double t0 = now_ms(), lock_ms = 0, lum_sum = 0;
    for (int f = 0; f < frames; f++) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageA(&msg);
        IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff0000ff, 1.0f, 0);
        IDirect3DDevice9_BeginScene(dev);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tex);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
        for (int k = 0; k < K; k++)
            IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, tq, sizeof *tq);
        IDirect3DDevice9_SetTexture(dev, 0, NULL);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, cq, sizeof *cq);
        /* luminance pass: textured quad into the 16x16 R32F target, read it back right away */
        IDirect3DDevice9_SetRenderTarget(dev, 0, lum);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tex);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, lq, sizeof *lq);
        IDirect3DDevice9_EndScene(dev);
        hr = IDirect3DDevice9_GetRenderTargetData(dev, lum, lumsys);
        if (FAILED(hr)) { printf("frame %d: GetRenderTargetData %08lx\n", f, hr); bad++; }
        double tl = now_ms();
        if (SUCCEEDED(IDirect3DSurface9_LockRect(lumsys, &lr, NULL, D3DLOCK_READONLY))) {
            float v = ((float *)lr.pBits)[0];
            IDirect3DSurface9_UnlockRect(lumsys);
            lum_sum += v;
            if (f % CHECK == CHECK - 1 && f >= CHECK && v != 1.0f) { printf("frame %d: luminance %f, want 1.0\n", f, v); bad++; }
        } else { printf("frame %d: luminance lock failed\n", f); bad++; }
        lock_ms += now_ms() - tl;
        if (f % CHECK == CHECK - 1) {
            IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
            hr = IDirect3DDevice9_GetRenderTargetData(dev, bb, bbsys);
            if (FAILED(hr)) { printf("frame %d: back buffer GetRenderTargetData %08lx\n", f, hr); bad++; }
            bad += check_pixel(bbsys, 20, 20, 0x0000ff, "background", f);
            bad += check_pixel(bbsys, 44, 44, 0xff0000, "textured red", f);
            bad += check_pixel(bbsys, 77, 44, 0xffffff, "textured white", f);
            bad += check_pixel(bbsys, 470, 180, 0x00ff00, "untextured green", f);
        }
        IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    }
    double total = now_ms() - t0;
    printf("d3d9rb: %d frames, K=%d: %.2f ms/frame (%.1f fps), readback lock %.2f ms/frame, mean luminance %.3f, bad checks %d\n",
           frames, K, total / frames, 1000.0 * frames / total, lock_ms / frames, lum_sum / frames, bad);
    fflush(stdout);
    return bad ? 1 : 0;
}
