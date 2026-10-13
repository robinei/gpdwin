/* d3d9depth: does hierarchical depth (HiZ) work for D3D9 depth buffers through DXVK on hasvk (CHV)?
 * Draws LAYERS full-screen quads front to back with depth test on, so every layer after the first is
 * hidden: HiZ should reject them in bulk (OA counter HiDepthTestFails), otherwise early depth tests
 * every pixel (EarlyDepthTestFails). Runs for SECONDS so i915-perf-recorder can sample it.
 *   mode 0: plain D24S8 depth surface (CreateDepthStencilSurface)
 *   mode 1: INTZ depth texture (readable depth, as games use for DoF/SSAO), its surface bound as depth
 * Build: zig cc -target x86_64-windows-gnu -O1 -o d3d9depth.exe d3d9depth.c -ld3d9 -lgdi32 -luser32
 * Run:   wine d3d9depth.exe MODE [seconds]   (DXVK d3d9.dll next to it, WINEDLLOVERRIDES=d3d9=n) */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

#define W 960
#define H 540
#define LAYERS 200
#define FOURCC_INTZ ((D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'))

struct v { float x, y, z, rhw; DWORD c; };

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

int main(int argc, char **argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : 0, seconds = argc > 2 ? atoi(argv[2]) : 10;
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d9depth";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3d9depth", "d3d9depth", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, W, H, NULL, NULL, wc.hInstance, NULL);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = { 0 };
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *dev;
    HRESULT hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { printf("CreateDevice failed %08lx\n", hr); return 1; }

    IDirect3DSurface9 *ds = NULL;
    IDirect3DTexture9 *dtex = NULL;
    if (mode == 0) {
        hr = IDirect3DDevice9_CreateDepthStencilSurface(dev, W, H, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds, NULL);
    } else {
        hr = IDirect3DDevice9_CreateTexture(dev, W, H, 1, D3DUSAGE_DEPTHSTENCIL, FOURCC_INTZ, D3DPOOL_DEFAULT, &dtex, NULL);
        if (SUCCEEDED(hr))
            hr = IDirect3DTexture9_GetSurfaceLevel(dtex, 0, &ds);
    }
    if (FAILED(hr)) { printf("depth creation (mode %d) failed %08lx\n", mode, hr); return 1; }
    IDirect3DDevice9_SetDepthStencilSurface(dev, ds);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, D3DZB_TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    DWORD end = GetTickCount() + seconds * 1000, frames = 0;
    while (GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageA(&msg);
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000040, 1.0f, 0);
        IDirect3DDevice9_BeginScene(dev);
        for (int i = 0; i < LAYERS; i++) { /* front to back: layer 0 nearest */
            float z = 0.1f + 0.8f * i / LAYERS;
            DWORD c = 0xff000000 | ((i * 37) & 255) << 16 | ((i * 91) & 255) << 8 | 0x40;
            struct v q[4] = { { 0, 0, z, 1, c }, { W, 0, z, 1, c }, { 0, H, z, 1, c }, { W, H, z, 1, c } };
            IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
        }
        IDirect3DDevice9_EndScene(dev);
        IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
        frames++;
    }
    printf("mode %d (%s): %lu frames in %d s = %.1f fps, %d hidden layers per frame\n", mode,
           mode ? "INTZ depth texture" : "D24S8 depth surface", frames, seconds, frames / (double)seconds, LAYERS - 1);
    fflush(stdout);
    return 0;
}
