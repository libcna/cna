// DX2-0 spike round 9 (Phase O4 pre-work): confirm a surface created with BOTH
// DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE works for both 2D Lock()/Blt() (Phase O2/O3's existing
// texture usage) and 3D IDirect3DTexture2 sampling via DrawIndexedPrimitive (Phase O4) -- needed
// because Dx2TextureBackend's surfaces currently have DDSCAPS_OFFSCREENPLAIN only, and adding a
// second cap after CreateSurface is not possible (caps are immutable), so this must be spiked
// before deciding to add DDSCAPS_TEXTURE to every texture's creation call.
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
    wc.lpszClassName = L"Dx2Spike9Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike9Window", L"DX2 Spike9",
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

    // The key test: DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE together, no explicit pixel format
    // (matching CreateOffscreenSurface's existing convention of letting Wine pick the native
    // format, then DetectChannelLayout probes it -- avoids the DDERR_INVALIDPIXELFORMAT rejection
    // DX1's own post-ship bug report already found when requesting an explicit format).
    DDSURFACEDESC texDesc; memset(&texDesc, 0, sizeof(texDesc));
    texDesc.dwSize = sizeof(texDesc);
    texDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    texDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE;
    texDesc.dwWidth = 2; texDesc.dwHeight = 2;
    LPDIRECTDRAWSURFACE texSurf = nullptr;
    hr = dd->CreateSurface(&texDesc, &texSurf, nullptr);
    PrintHr("CreateSurface(OFFSCREENPLAIN|TEXTURE, no explicit format)", hr);

    if (FAILED(hr))
    {
        printf("Falling back to TEXTURE-only cap (no OFFSCREENPLAIN)...\n");
        texDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
        hr = dd->CreateSurface(&texDesc, &texSurf, nullptr);
        PrintHr("CreateSurface(TEXTURE only)", hr);
    }

    // 2D-style usage: Lock() + write raw pixels directly (same mechanism FillSurfaceColor/
    // WriteSurfacePixels already use for Dx2TextureBackend).
    DDSURFACEDESC lockDesc; memset(&lockDesc, 0, sizeof(lockDesc));
    lockDesc.dwSize = sizeof(lockDesc);
    hr = texSurf->Lock(nullptr, &lockDesc, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(2D-style write)", hr);
    if (SUCCEEDED(hr))
    {
        printf("locked: lPitch=%ld dwRGBBitCount=%lu\n",
               (long)lockDesc.lPitch, (unsigned long)lockDesc.ddpfPixelFormat.dwRGBBitCount);
        auto* row0 = (uint32_t*)((uint8_t*)lockDesc.lpSurface + 0 * lockDesc.lPitch);
        auto* row1 = (uint32_t*)((uint8_t*)lockDesc.lpSurface + 1 * lockDesc.lPitch);
        row0[0] = 0xFFFF0000u; row0[1] = 0xFF0000FFu;
        row1[0] = 0xFF0000FFu; row1[1] = 0xFFFF0000u;
        texSurf->Unlock(lockDesc.lpSurface);
    }

    // 3D-style usage: QueryInterface for IDirect3DTexture2 on the SAME surface, GetHandle, bind,
    // sample via DrawIndexedPrimitive.
    LPDIRECT3DTEXTURE2 tex2 = nullptr;
    hr = texSurf->QueryInterface(IID_IDirect3DTexture2, (void**)&tex2);
    PrintHr("QueryInterface(IDirect3DTexture2) on the SAME dual-cap surface", hr);
    D3DTEXTUREHANDLE texHandle = 0;
    if (tex2)
    {
        hr = tex2->GetHandle(device, &texHandle);
        PrintHr("GetHandle", hr);
    }

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
    D3DRECT clearRect{0, 0, 64, 64};
    viewport->Clear(1, &clearRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);

    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, texHandle);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATE);

    D3DTLVERTEX verts[4];
    memset(verts, 0, sizeof(verts));
    verts[0].sx = 0;  verts[0].sy = 0;  verts[0].sz = 0.5f; verts[0].rhw = 1.0f; verts[0].color = 0xFFFFFFFFu; verts[0].tu = 0.0f; verts[0].tv = 0.0f;
    verts[1].sx = 64; verts[1].sy = 0;  verts[1].sz = 0.5f; verts[1].rhw = 1.0f; verts[1].color = 0xFFFFFFFFu; verts[1].tu = 1.0f; verts[1].tv = 0.0f;
    verts[2].sx = 64; verts[2].sy = 64; verts[2].sz = 0.5f; verts[2].rhw = 1.0f; verts[2].color = 0xFFFFFFFFu; verts[2].tu = 1.0f; verts[2].tv = 1.0f;
    verts[3].sx = 0;  verts[3].sy = 64; verts[3].sz = 0.5f; verts[3].rhw = 1.0f; verts[3].color = 0xFFFFFFFFu; verts[3].tu = 0.0f; verts[3].tv = 1.0f;
    WORD idx[6] = { 0,1,2, 0,2,3 };

    hr = device->BeginScene();
    PrintHr("BeginScene", hr);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, verts, 4, idx, 6, 0);
    PrintHr("DrawIndexedPrimitive (sampling the dual-cap surface)", hr);
    hr = device->EndScene();
    PrintHr("EndScene", hr);

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
    printf("SPIKE9-DONE (expect the 2x2 texture's 4-quadrant pattern, matching dx2_spike7's own texture test)\n");
    return 0;
}
