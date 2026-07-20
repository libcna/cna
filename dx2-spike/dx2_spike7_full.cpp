// DX2-0 spike round 7: validate the remaining IDirect3DDevice2 3D surface Dx2GraphicsBackend
// will actually need: DrawIndexedPrimitive, Z-buffer test truly enabled (two overlapping
// triangles at different depths must occlude correctly, not just "not crash"), and a real
// bound + sampled texture via the (now proven-working) DrawPrimitive path.
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
static void DumpSurface(LPDIRECTDRAWSURFACE surf, const char* label) {
    DDSURFACEDESC d; memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    HRESULT hr = surf->Lock(nullptr, &d, DDLOCK_WAIT, nullptr);
    if (FAILED(hr)) { printf("%s: lock failed\n", label); return; }
    auto* base = (uint8_t*)d.lpSurface;
    for (int y = 4; y < 64; y += 8) {
        printf("%s row %2d:", label, y);
        for (int x = 4; x < 64; x += 8) {
            uint8_t* p = base + y * d.lPitch + x * 4;
            printf(" (%3d,%3d,%3d)", p[2], p[1], p[0]);
        }
        printf("\n");
    }
    surf->Unlock(d.lpSurface);
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx2Spike7Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike7Window", L"DX2 Spike7",
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
    hr = viewport->Clear(1, &clearRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
    PrintHr("Clear", hr);

    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);

    // --- Test A: DrawIndexedPrimitive, two overlapping quads (4 tri each via shared verts),
    // near quad RED at z=0.2, far quad GREEN at z=0.8, Z-TEST ENABLED. Only red should show.
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESS);
    D3DTLVERTEX quadVerts[8];
    memset(quadVerts, 0, sizeof(quadVerts));
    // near (red) quad covering the whole 64x64 viewport, z=0.2 (closer)
    quadVerts[0].sx = 0;  quadVerts[0].sy = 0;  quadVerts[0].sz = 0.2f; quadVerts[0].rhw = 1.0f; quadVerts[0].color = 0xFFFF0000;
    quadVerts[1].sx = 64; quadVerts[1].sy = 0;  quadVerts[1].sz = 0.2f; quadVerts[1].rhw = 1.0f; quadVerts[1].color = 0xFFFF0000;
    quadVerts[2].sx = 64; quadVerts[2].sy = 64; quadVerts[2].sz = 0.2f; quadVerts[2].rhw = 1.0f; quadVerts[2].color = 0xFFFF0000;
    quadVerts[3].sx = 0;  quadVerts[3].sy = 64; quadVerts[3].sz = 0.2f; quadVerts[3].rhw = 1.0f; quadVerts[3].color = 0xFFFF0000;
    // far (green) quad covering the whole viewport too, z=0.8 (farther) -- should be occluded
    quadVerts[4].sx = 0;  quadVerts[4].sy = 0;  quadVerts[4].sz = 0.8f; quadVerts[4].rhw = 1.0f; quadVerts[4].color = 0xFF00FF00;
    quadVerts[5].sx = 64; quadVerts[5].sy = 0;  quadVerts[5].sz = 0.8f; quadVerts[5].rhw = 1.0f; quadVerts[5].color = 0xFF00FF00;
    quadVerts[6].sx = 64; quadVerts[6].sy = 64; quadVerts[6].sz = 0.8f; quadVerts[6].rhw = 1.0f; quadVerts[6].color = 0xFF00FF00;
    quadVerts[7].sx = 0;  quadVerts[7].sy = 64; quadVerts[7].sz = 0.8f; quadVerts[7].rhw = 1.0f; quadVerts[7].color = 0xFF00FF00;
    // far quad drawn FIRST (indices 4..7), near quad drawn SECOND (indices 0..3) -- if Z-test
    // works, red (drawn second, closer) should win over green (drawn first, farther) everywhere.
    WORD idx[12] = { 4,5,6, 4,6,7,  0,1,2, 0,2,3 };

    hr = device->BeginScene();
    PrintHr("BeginScene(A)", hr);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, quadVerts, 8, idx, 12, 0);
    PrintHr("DrawIndexedPrimitive(A)", hr);
    hr = device->EndScene();
    PrintHr("EndScene(A)", hr);
    DumpSurface(rt, "A(z-test)");

    // --- Test B: texture sampling via DrawPrimitive (not execute-buffer). 2x2 texture:
    // top-left/bottom-right RED, top-right/bottom-left BLUE, modulated with WHITE vertices
    // so the sampled texture color should show through unmodified.
    viewport->Clear(1, &clearRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);

    DDSURFACEDESC texDesc; memset(&texDesc, 0, sizeof(texDesc));
    texDesc.dwSize = sizeof(texDesc);
    texDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    texDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    texDesc.dwWidth = 2; texDesc.dwHeight = 2;
    texDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    texDesc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    texDesc.ddpfPixelFormat.dwRGBBitCount = 32;
    texDesc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
    texDesc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
    texDesc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
    texDesc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
    LPDIRECTDRAWSURFACE texSurf = nullptr;
    hr = dd->CreateSurface(&texDesc, &texSurf, nullptr);
    PrintHr("CreateSurface(tex)", hr);
    DDSURFACEDESC texLock; memset(&texLock, 0, sizeof(texLock));
    texLock.dwSize = sizeof(texLock);
    hr = texSurf->Lock(nullptr, &texLock, DDLOCK_WAIT, nullptr);
    if (SUCCEEDED(hr)) {
        auto* row0 = (uint32_t*)((uint8_t*)texLock.lpSurface + 0 * texLock.lPitch);
        auto* row1 = (uint32_t*)((uint8_t*)texLock.lpSurface + 1 * texLock.lPitch);
        row0[0] = 0xFFFF0000u; row0[1] = 0xFF0000FFu; // red, blue
        row1[0] = 0xFF0000FFu; row1[1] = 0xFFFF0000u; // blue, red
        texSurf->Unlock(texLock.lpSurface);
    }
    LPDIRECT3DTEXTURE2 tex2 = nullptr;
    hr = texSurf->QueryInterface(IID_IDirect3DTexture2, (void**)&tex2);
    PrintHr("QueryInterface(IDirect3DTexture2)", hr);
    D3DTEXTUREHANDLE texHandle = 0;
    hr = tex2->GetHandle(device, &texHandle);
    PrintHr("GetHandle", hr);

    device->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, texHandle);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATE);

    D3DTLVERTEX texVerts[4];
    memset(texVerts, 0, sizeof(texVerts));
    texVerts[0].sx = 0;  texVerts[0].sy = 0;  texVerts[0].sz = 0.5f; texVerts[0].rhw = 1.0f; texVerts[0].color = 0xFFFFFFFF; texVerts[0].tu = 0.0f; texVerts[0].tv = 0.0f;
    texVerts[1].sx = 64; texVerts[1].sy = 0;  texVerts[1].sz = 0.5f; texVerts[1].rhw = 1.0f; texVerts[1].color = 0xFFFFFFFF; texVerts[1].tu = 1.0f; texVerts[1].tv = 0.0f;
    texVerts[2].sx = 64; texVerts[2].sy = 64; texVerts[2].sz = 0.5f; texVerts[2].rhw = 1.0f; texVerts[2].color = 0xFFFFFFFF; texVerts[2].tu = 1.0f; texVerts[2].tv = 1.0f;
    texVerts[3].sx = 0;  texVerts[3].sy = 64; texVerts[3].sz = 0.5f; texVerts[3].rhw = 1.0f; texVerts[3].color = 0xFFFFFFFF; texVerts[3].tu = 0.0f; texVerts[3].tv = 1.0f;
    WORD texIdx[6] = { 0,1,2, 0,2,3 };

    hr = device->BeginScene();
    PrintHr("BeginScene(B)", hr);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, texVerts, 4, texIdx, 6, 0);
    PrintHr("DrawIndexedPrimitive(B textured)", hr);
    hr = device->EndScene();
    PrintHr("EndScene(B)", hr);
    DumpSurface(rt, "B(texture)");

    printf("SPIKE7-DONE\n");
    return 0;
}
