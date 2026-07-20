// DX2-0 spike round 8: validate manually Lock()-ing the Z-buffer surface and writing a raw
// initial depth value (needed for ClearDepth/ClearColorAndDepth, since v1/v2 IDirect3DViewport::
// Clear() has no depth-VALUE parameter -- only IDirect3DViewport3::Clear2 (DX5+) has one). Confirms:
// 1) the locked Z-buffer surface's pixel format/pitch for a 16-bit DDSCAPS_ZBUFFER surface,
// 2) that a manually-written initial Z value is correctly respected by a subsequent real
//    DrawIndexedPrimitive's depth test (not just a real Draw's own Z-write, which is already
//    spike-proven in dx2_spike7_full.cpp test A).
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
    wc.lpszClassName = L"Dx2Spike8Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike8Window", L"DX2 Spike8",
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

    // Inspect the Z-buffer surface's own reported pixel format / pitch before writing anything.
    DDSURFACEDESC zProbe; memset(&zProbe, 0, sizeof(zProbe));
    zProbe.dwSize = sizeof(zProbe);
    hr = zbuf->Lock(nullptr, &zProbe, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(zbuf probe)", hr);
    if (SUCCEEDED(hr)) {
        printf("zbuf: lPitch=%ld dwRGBBitCount(reused as Z bit count)=%lu\n",
               (long)zProbe.lPitch, (unsigned long)zProbe.ddpfPixelFormat.dwZBufferBitDepth);
        printf("zbuf: ddpfPixelFormat.dwFlags=0x%08lx dwSize=%lu\n",
               (unsigned long)zProbe.ddpfPixelFormat.dwFlags, (unsigned long)zProbe.ddpfPixelFormat.dwSize);
        // Manually write a "near" depth (small value = close to viewer) into the WHOLE surface.
        // 16-bit Z-buffers historically store an unsigned value proportional to depth: 0=near,
        // 0xFFFF=far, linearly mapped from dvMinZ..dvMaxZ.
        const uint16_t nearValue = 0x1000;  // very close to the viewer
        for (int y = 0; y < 64; ++y) {
            auto* row = (uint16_t*)((uint8_t*)zProbe.lpSurface + y * zProbe.lPitch);
            for (int x = 0; x < 64; ++x) row[x] = nearValue;
        }
        zbuf->Unlock(zProbe.lpSurface);
    }

    LPDIRECT3D2 d3d2 = nullptr;
    hr = dd->QueryInterface(IID_IDirect3D2, (void**)&d3d2);
    PrintHr("QueryInterface(IDirect3D2)", hr);
    LPDIRECT3DDEVICE2 device = nullptr;
    hr = d3d2->CreateDevice(IID_IDirect3DRGBDevice, rt, &device);
    PrintHr("CreateDevice(RGBDevice2)", hr);

    LPDIRECT3DVIEWPORT2 viewport = nullptr;
    d3d2->CreateViewport(&viewport, nullptr);
    device->AddViewport(viewport);
    D3DVIEWPORT2 vp; memset(&vp, 0, sizeof(vp));
    vp.dwSize = sizeof(vp);
    vp.dwWidth = 64; vp.dwHeight = 64;
    vp.dvClipX = -1.0f; vp.dvClipY = 1.0f;
    vp.dvClipWidth = 2.0f; vp.dvClipHeight = 2.0f;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    viewport->SetViewport2(&vp);
    device->SetCurrentViewport(viewport);

    // Deliberately do NOT call viewport->Clear() here -- the point is to test whether the
    // MANUALLY-written Z-buffer content (above) alone is respected by the depth test, without any
    // D3D-side clear touching it.
    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESS);

    // Draw a FAR (z=0.9) full-viewport GREEN quad. If the manually-written near Z (0x1000, very
    // close) is genuinely respected by the depth test, this far quad should be entirely REJECTED
    // -- the framebuffer should show whatever the color buffer already had (never touched here,
    // so real Wine's own default/undefined content) rather than green, proving the manual Z-buffer
    // write actually blocked the draw.
    D3DTLVERTEX verts[4];
    memset(verts, 0, sizeof(verts));
    verts[0].sx = 0;  verts[0].sy = 0;  verts[0].sz = 0.9f; verts[0].rhw = 1.0f; verts[0].color = 0xFF00FF00;
    verts[1].sx = 64; verts[1].sy = 0;  verts[1].sz = 0.9f; verts[1].rhw = 1.0f; verts[1].color = 0xFF00FF00;
    verts[2].sx = 64; verts[2].sy = 64; verts[2].sz = 0.9f; verts[2].rhw = 1.0f; verts[2].color = 0xFF00FF00;
    verts[3].sx = 0;  verts[3].sy = 64; verts[3].sz = 0.9f; verts[3].rhw = 1.0f; verts[3].color = 0xFF00FF00;
    WORD idx[6] = { 0,1,2, 0,2,3 };

    // First, paint the color buffer solid RED via a Z-disabled draw so we have a known baseline
    // to check "did the green quad get rejected" against.
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    D3DTLVERTEX redVerts[4];
    memcpy(redVerts, verts, sizeof(verts));
    for (auto& v : redVerts) { v.color = 0xFFFF0000; v.sz = 0.5f; }
    hr = device->BeginScene();
    device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, redVerts, 4, idx, 6, 0);
    hr = device->EndScene();
    PrintHr("Paint baseline red", hr);

    // Now re-enable Z-test and attempt the far green quad against the manually-written near Z.
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    hr = device->BeginScene();
    PrintHr("BeginScene(green attempt)", hr);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, verts, 4, idx, 6, 0);
    PrintHr("DrawIndexedPrimitive(green attempt)", hr);
    hr = device->EndScene();
    PrintHr("EndScene(green attempt)", hr);

    DDSURFACEDESC readDesc; memset(&readDesc, 0, sizeof(readDesc));
    readDesc.dwSize = sizeof(readDesc);
    hr = rt->Lock(nullptr, &readDesc, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(readback)", hr);
    if (SUCCEEDED(hr)) {
        auto* pixBase = (uint8_t*)readDesc.lpSurface;
        for (int y = 8; y < 56; y += 12) {
            printf("row %2d:", y);
            for (int x = 8; x < 56; x += 12) {
                uint8_t* p = pixBase + y * readDesc.lPitch + x * 4;
                printf(" (%3d,%3d,%3d)", p[2], p[1], p[0]);
            }
            printf("\n");
        }
        rt->Unlock(readDesc.lpSurface);
    }
    printf("SPIKE8-DONE (expect RED everywhere if manual Z-write blocked the far green quad)\n");
    return 0;
}
