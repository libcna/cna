// DX8 follow-up spike: isolate why IDirect3DDevice8::CopyRects fails with D3DERR_INVALIDCALL when
// copying FROM a D3DUSAGE_RENDERTARGET texture's surface (obtained via GetSurfaceLevel) TO a
// CreateImageSurface system-memory surface -- the exact scenario Dx8GraphicsBackend::ReadBackbuffer
// hit on the first full CTest run. Real D3D8 docs describe this as the canonical way to capture
// render-target contents before D3D9 added GetRenderTargetData, so this should work -- confirm
// exactly which part of the setup breaks it.
#include <windows.h>
#include <d3d8.h>
#include <cstdio>
#include <cstring>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void PrintHr(const char* what, HRESULT hr) {
    printf("%s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx8Spike2Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx8Spike2Window", L"DX8 Spike2",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    IDirect3D8* d3d8 = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d8) { printf("Direct3DCreate8 failed\n"); return 1; }

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    IDirect3DDevice8* device = nullptr;
    HRESULT hr = d3d8->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    PrintHr("CreateDevice", hr);
    if (FAILED(hr)) return 1;

    // --- Test A: CopyRects from the real SWAP CHAIN back buffer (GetBackBuffer) -- known to work
    // already (this exact call succeeds in Dx8GraphicsBackend's own logical-quad-based Present()
    // path, and was used throughout the first spike's own SamplePixel helper).
    IDirect3DSurface8* swapChainBB = nullptr;
    hr = device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &swapChainBB);
    PrintHr("A: GetBackBuffer", hr);
    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 10, 20, 30), 1.0f, 0);
    IDirect3DSurface8* sysmemA = nullptr;
    hr = device->CreateImageSurface(64, 64, D3DFMT_A8R8G8B8, &sysmemA);
    PrintHr("A: CreateImageSurface", hr);
    hr = device->CopyRects(swapChainBB, nullptr, 0, sysmemA, nullptr);
    PrintHr("A: CopyRects(swapChainBackBuffer -> sysmem)", hr);

    // --- Test B: create a D3DUSAGE_RENDERTARGET texture (matching Dx8's own logical render
    // target), get its surface via GetSurfaceLevel, make it the ACTIVE render target, clear it,
    // then CopyRects from THAT surface -- the exact scenario that failed.
    IDirect3DTexture8* rtTex = nullptr;
    hr = device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTex);
    PrintHr("B: CreateTexture(RENDERTARGET)", hr);
    IDirect3DSurface8* rtSurf = nullptr;
    hr = rtTex->GetSurfaceLevel(0, &rtSurf);
    PrintHr("B: GetSurfaceLevel", hr);
    hr = device->SetRenderTarget(rtSurf, nullptr);
    PrintHr("B: SetRenderTarget(rtSurf, no depth-stencil)", hr);
    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 40, 50, 60), 1.0f, 0);
    PrintHr("B: Clear(rtSurf active)", hr);
    IDirect3DSurface8* sysmemB = nullptr;
    hr = device->CreateImageSurface(64, 64, D3DFMT_A8R8G8B8, &sysmemB);
    PrintHr("B: CreateImageSurface", hr);
    hr = device->CopyRects(rtSurf, nullptr, 0, sysmemB, nullptr);
    PrintHr("B: CopyRects(rtSurf active as RT -> sysmem)", hr);

    // --- Test C: same as B, but switch the active render target back to the swap chain FIRST
    // (rtSurf no longer bound as active), then CopyRects from rtSurf while it's not the active RT.
    hr = device->SetRenderTarget(swapChainBB, nullptr);
    PrintHr("C: SetRenderTarget(back to swap chain)", hr);
    IDirect3DSurface8* sysmemC = nullptr;
    hr = device->CreateImageSurface(64, 64, D3DFMT_A8R8G8B8, &sysmemC);
    hr = device->CopyRects(rtSurf, nullptr, 0, sysmemC, nullptr);
    PrintHr("C: CopyRects(rtSurf NOT active -> sysmem)", hr);

    // --- Test D: same as B/C but pass an explicit full-surface RECT instead of nullptr (mirroring
    // this backend family's own recurring "count=0/rects=nullptr means something different than
    // expected" gotcha, found for DX5's Clear2 and re-checked for DX7/DX8's own device-Clear).
    RECT fullRect{0, 0, 64, 64};
    POINT dstPoint{0, 0};
    IDirect3DSurface8* sysmemD = nullptr;
    hr = device->CreateImageSurface(64, 64, D3DFMT_A8R8G8B8, &sysmemD);
    hr = device->CopyRects(rtSurf, &fullRect, 1, sysmemD, &dstPoint);
    PrintHr("D: CopyRects(rtSurf, explicit full RECT+POINT, not active) -> sysmem)", hr);

    // --- Test E: exactly matching Dx8GraphicsBackend's own constructor sequence -- a REAL
    // CreateDepthStencilSurface attached alongside the render-target texture's surface, then
    // CopyRects from that surface while it is (and then is not) the active render target.
    IDirect3DTexture8* rtTexE = nullptr;
    hr = device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTexE);
    PrintHr("E: CreateTexture(RENDERTARGET)", hr);
    IDirect3DSurface8* rtSurfE = nullptr;
    hr = rtTexE->GetSurfaceLevel(0, &rtSurfE);
    PrintHr("E: GetSurfaceLevel", hr);
    IDirect3DSurface8* dsE = nullptr;
    hr = device->CreateDepthStencilSurface(64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &dsE);
    PrintHr("E: CreateDepthStencilSurface", hr);
    hr = device->SetRenderTarget(rtSurfE, dsE);
    PrintHr("E: SetRenderTarget(rtSurfE, dsE) -- real depth-stencil attached", hr);
    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        D3DCOLOR_ARGB(255, 70, 80, 90), 1.0f, 0);
    PrintHr("E: Clear(TARGET|ZBUFFER|STENCIL, rtSurfE active)", hr);
    IDirect3DSurface8* sysmemE = nullptr;
    hr = device->CreateImageSurface(64, 64, D3DFMT_A8R8G8B8, &sysmemE);
    PrintHr("E: CreateImageSurface", hr);
    hr = device->CopyRects(rtSurfE, nullptr, 0, sysmemE, nullptr);
    PrintHr("E: CopyRects(rtSurfE active, real depth-stencil attached -> sysmem)", hr);

    printf("DX8-SPIKE2-DONE\n");
    return 0;
}
