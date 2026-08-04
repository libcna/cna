// DX2-0 spike round 6: test IDirect3D2/IDirect3DDevice2's DrawPrimitive immediate-mode path
// (added in the DirectX 3 SDK, alongside execute buffers) as a diagnostic to see whether
// Wine's DrawPrimitive code path (much more commonly exercised historically than the ancient
// execute-buffer opcode interpreter) also fails, or whether it's specific to execute buffers.
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

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
    wc.lpszClassName = L"Dx2Spike6Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike6Window", L"DX2 Spike6 (v2 DrawPrimitive)",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    LPDIRECTDRAW dd = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &dd, nullptr);
    PrintHr("DirectDrawCreate", hr);
    dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL);

    DDSURFACEDESC rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
    rtDesc.dwSize = sizeof(rtDesc);
    rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    rtDesc.dwWidth = 64; rtDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE rt = nullptr;
    hr = dd->CreateSurface(&rtDesc, &rt, nullptr);
    PrintHr("CreateSurface(rt)", hr);

    DDSURFACEDESC zDesc; memset(&zDesc, 0, sizeof(zDesc));
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    zDesc.dwWidth = 64; zDesc.dwHeight = 64; zDesc.dwZBufferBitDepth = 16;
    LPDIRECTDRAWSURFACE zbuf = nullptr;
    hr = dd->CreateSurface(&zDesc, &zbuf, nullptr);
    PrintHr("CreateSurface(zbuf)", hr);
    rt->AddAttachedSurface(zbuf);

    LPDIRECT3D2 d3d2 = nullptr;
    hr = dd->QueryInterface(IID_IDirect3D2, (void**)&d3d2);
    PrintHr("QueryInterface(IDirect3D2)", hr);

    LPDIRECT3DDEVICE2 device = nullptr;
    hr = d3d2->CreateDevice(IID_IDirect3DRGBDevice, rt, &device);
    PrintHr("CreateDevice(RGBDevice2)", hr);

    LPDIRECT3DVIEWPORT2 viewport = nullptr;
    hr = d3d2->CreateViewport(&viewport, nullptr);
    PrintHr("CreateViewport2", hr);
    hr = device->AddViewport(viewport);
    PrintHr("AddViewport", hr);
    D3DVIEWPORT2 vp; memset(&vp, 0, sizeof(vp));
    vp.dwSize = sizeof(vp);
    vp.dwWidth = 64; vp.dwHeight = 64;
    vp.dvClipX = -1.0f; vp.dvClipY = 1.0f;
    vp.dvClipWidth = 2.0f; vp.dvClipHeight = 2.0f;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    hr = viewport->SetViewport2(&vp);
    PrintHr("SetViewport2", hr);
    hr = device->SetCurrentViewport(viewport);
    PrintHr("SetCurrentViewport", hr);

    D3DRECT clearRect{0, 0, 64, 64};
    hr = viewport->Clear(1, &clearRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
    PrintHr("Clear", hr);

    hr = device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    PrintHr("SetRenderState(CULLMODE)", hr);
    hr = device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    PrintHr("SetRenderState(ZENABLE)", hr);
    hr = device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    PrintHr("SetRenderState(LIGHTING)", hr);

    D3DTLVERTEX verts[3];
    memset(verts, 0, sizeof(verts));
    verts[0].sx = -200.0f; verts[0].sy = -200.0f; verts[0].sz = 0.5f; verts[0].rhw = 1.0f; verts[0].color = 0xFFFF0000;
    verts[1].sx = -200.0f; verts[1].sy = 400.0f;  verts[1].sz = 0.5f; verts[1].rhw = 1.0f; verts[1].color = 0xFF00FF00;
    verts[2].sx = 400.0f;  verts[2].sy = 400.0f;  verts[2].sz = 0.5f; verts[2].rhw = 1.0f; verts[2].color = 0xFF0000FF;

    hr = device->BeginScene();
    PrintHr("BeginScene", hr);
    hr = device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, verts, 3, 0);
    PrintHr("DrawPrimitive", hr);
    hr = device->EndScene();
    PrintHr("EndScene", hr);

    DDSURFACEDESC readDesc; memset(&readDesc, 0, sizeof(readDesc));
    readDesc.dwSize = sizeof(readDesc);
    hr = rt->Lock(nullptr, &readDesc, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(readback)", hr);
    if (SUCCEEDED(hr)) {
        auto* pixBase = (uint8_t*)readDesc.lpSurface;
        for (int y = 8; y < 56; y += 6) {
            printf("row %2d:", y);
            for (int x = 8; x < 56; x += 6) {
                uint8_t* p = pixBase + y * readDesc.lPitch + x * 4;
                printf(" (%3d,%3d,%3d)", p[2], p[1], p[0]);
            }
            printf("\n");
        }
        rt->Unlock(readDesc.lpSurface);
    }
    printf("SPIKE6-DONE\n");
    return 0;
}
