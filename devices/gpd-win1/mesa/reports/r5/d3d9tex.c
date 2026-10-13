/* d3d9tex: minimal Direct3D 9 test for DXVK on hasvk (mesa/reports/R5-softpin-vulkan13.md, field test).
 * Draws a textured quad (left, red/white checkerboard) and an untextured one (right, green) on blue, for
 * FRAMES frames, then reads back the render target and prints the colour at the centre of each quad. DXVK
 * 2.6.2 + the claim-1.3 driconf showed only untextured draws in Dark Souls; this tells whether texturing
 * fails in rendering (readback wrong) or only on screen (readback right, screen black).
 * Build (desktop): zig cc -target x86_64-windows-gnu -O1 -o d3d9tex.exe d3d9tex.c -ld3d9 -lgdi32 -luser32
 * Run: wine d3d9tex.exe [frames]  (with DXVK's d3d9.dll next to it and WINEDLLOVERRIDES=d3d9=n) */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

#define W 640
#define H 360

struct tv { float x, y, z, rhw, u, v; };
struct cv { float x, y, z, rhw; DWORD c; };

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static void pixel(IDirect3DSurface9 *s, int x, int y, const char *what)
{
    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DSurface9_LockRect(s, &lr, NULL, D3DLOCK_READONLY))) {
        printf("%s: lock failed\n", what);
        return;
    }
    DWORD c = ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x];
    printf("%-26s (%3d,%3d): r=%3lu g=%3lu b=%3lu\n", what, x, y, (c >> 16) & 255, (c >> 8) & 255, c & 255);
    IDirect3DSurface9_UnlockRect(s);
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 120;
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d9tex";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3d9tex", "d3d9tex", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, W, H, NULL, NULL, wc.hInstance, NULL);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { printf("Direct3DCreate9 failed\n"); return 1; }
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
    if (FAILED(hr)) { printf("CreateDevice failed %08lx\n", hr); return 1; }

    /* 64x64 checkerboard, red/white, 8 px squares */
    IDirect3DTexture9 *tex;
    IDirect3DDevice9_CreateTexture(dev, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    D3DLOCKED_RECT lr;
    IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = ((x / 8 + y / 8) & 1) ? 0xffffffff : 0xffff0000;
    IDirect3DTexture9_UnlockRect(tex, 0);

    struct tv tq[4] = { { 40, 40, 0, 1, 0, 0 }, { 300, 40, 0, 1, 1, 0 }, { 40, 320, 0, 1, 0, 1 }, { 300, 320, 0, 1, 1, 1 } };
    struct cv cq[4] = { { 340, 40, 0, 1, 0xff00ff00 }, { 600, 40, 0, 1, 0xff00ff00 },
                        { 340, 320, 0, 1, 0xff00ff00 }, { 600, 320, 0, 1, 0xff00ff00 } };
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

    for (int f = 0; f < frames; f++) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageA(&msg);
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff0000ff, 1.0f, 0);
        IDirect3DDevice9_BeginScene(dev);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tex);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, tq, sizeof *tq);
        IDirect3DDevice9_SetTexture(dev, 0, NULL);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, cq, sizeof *cq);
        IDirect3DDevice9_EndScene(dev);
        if (f == frames - 1) { /* read back before Present */
            IDirect3DSurface9 *bb, *sys;
            IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
            IDirect3DDevice9_CreateOffscreenPlainSurface(dev, W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL);
            hr = IDirect3DDevice9_GetRenderTargetData(dev, bb, sys);
            printf("GetRenderTargetData %08lx after %d frames\n", hr, frames);
            pixel(sys, 20, 20, "background (blue)");
            pixel(sys, 44, 44, "textured, red square");
            pixel(sys, 44 + 33, 44, "textured, white square");
            pixel(sys, 470, 180, "untextured (green)");
        }
        IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    }
    fflush(stdout);
    return 0;
}
