// DX7-0 existence-gate spike: confirm the genuinely new, structurally different parts of DirectX 7
// (1999) actually work in this environment's Wine before any backend code is written.
//
// Unlike DX2->DX30->DX5->DX6 (each either a mechanical interface-version bump or, for DX6, no new
// interface at all), DX7 is a real architectural change to the object graph:
//   1. IDirectDraw7/IDirectDrawSurface7 are NEW interfaces (confirmed in ddraw.h), and the header
//      itself documents "IDirectDraw7 cannot derive from IDirectDraw4; it is even documented as not
//      interchangeable with earlier DirectDraw interfaces" -- a real question of whether the usual
//      DirectDrawCreate()+QueryInterface() upgrade chain (used by every DX2..DX6 backend so far)
//      still works, or whether the new DirectDrawCreateEx() entry point is required instead.
//   2. IDirect3DViewport3 (and CreateViewport/AddViewport/SetCurrentViewport/DeleteViewport) is
//      GONE ENTIRELY in DX7 -- confirmed by inspecting d3d.h: IDirect3D7 has no CreateViewport
//      method at all, and IDirect3DDevice7::SetViewport takes a plain D3DVIEWPORT7 struct directly,
//      with Clear() now a DIRECT IDirect3DDevice7 method (not IDirect3DViewportN::Clear2 on a
//      separate viewport object). This is a real structural simplification of the whole rendering
//      setup this backend family has used since DX2-0.
//   3. IDirect3D7::CreateDevice drops the trailing IUnknown* outer parameter DX5/DX6's
//      IDirect3D3::CreateDevice had (a full circle: DX3/DX2's IDirect3D2::CreateDevice never had
//      it either).
//   4. IDirectDrawSurface7 keeps the same Unlock(LPRECT) shape IDirectDrawSurface4 already had (no
//      repeat of the DX5-0 Unlock-signature surprise) but adds SetPriority/GetPriority/SetLOD/
//      GetLOD (texture management, not used by this backend family's design).
//
// This spike tests, in order: (A) DirectDrawCreateEx vs the old QueryInterface upgrade chain, both
// against a real window; (B) a stencil-capable Z-buffer surface + IDirect3D7::CreateDevice with NO
// viewport object at all; (C) device-direct SetViewport(D3DVIEWPORT7)+Clear(); (D) DrawPrimitive
// with the same D3DFVF_TLVERTEX shape DX2..DX6 already use, confirming the flattened object model
// still renders Gouraud-shaded, textured geometry correctly; (E) stencil write+test through the new
// device-direct Clear(), confirming DX6's own real stencil capability survives the API flattening.
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
static void SamplePixel(LPDIRECTDRAWSURFACE7 surf, int x, int y, const char* label) {
    DDSURFACEDESC2 d; memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    HRESULT hr = surf->Lock(nullptr, &d, DDLOCK_WAIT, nullptr);
    if (FAILED(hr)) { printf("%s: lock failed hr=0x%08lx\n", label, (unsigned long)hr); return; }
    auto* p = (uint8_t*)d.lpSurface + y * d.lPitch + x * 4;
    printf("%s (%d,%d) = (%3d,%3d,%3d)\n", label, x, y, p[2], p[1], p[0]);
    surf->Unlock(nullptr);
}

static HRESULT CALLBACK EnumDevicesCallback(char* description, char* name, D3DDEVICEDESC7* desc, void* ctx) {
    printf("  EnumDevices7: name='%s' description='%s' dwDevCaps=0x%08lx\n",
           name, description, (unsigned long)desc->dwDevCaps);
    return D3DENUMRET_OK;
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx7Spike1Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx7Spike1Window", L"DX7 Spike1",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // --- Test A1: the OLD upgrade chain (DirectDrawCreate v1 + QueryInterface(IID_IDirectDraw7)) --
    // does it still work despite the header's "cannot derive from IDirectDraw4" note?
    LPDIRECTDRAW ddV1 = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &ddV1, nullptr);
    PrintHr("DirectDrawCreate (v1)", hr);
    LPDIRECTDRAW7 dd7FromQI = nullptr;
    if (SUCCEEDED(hr)) {
        hr = ddV1->QueryInterface(IID_IDirectDraw7, (void**)&dd7FromQI);
        PrintHr("A1: ddV1->QueryInterface(IID_IDirectDraw7)", hr);
        if (dd7FromQI) { dd7FromQI->Release(); dd7FromQI = nullptr; }
        ddV1->Release();
    }

    // --- Test A2: the NEW DX7 entry point, DirectDrawCreateEx, requesting IID_IDirectDraw7 directly.
    LPDIRECTDRAW7 dd7 = nullptr;
    hr = DirectDrawCreateEx(nullptr, (void**)&dd7, IID_IDirectDraw7, nullptr);
    PrintHr("A2: DirectDrawCreateEx(IID_IDirectDraw7)", hr);
    if (FAILED(hr)) { printf("DX7-SPIKE1-ABORT: DirectDrawCreateEx failed\n"); return 1; }

    hr = dd7->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    PrintHr("SetCooperativeLevel", hr);

    DDSURFACEDESC2 rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
    rtDesc.dwSize = sizeof(rtDesc);
    rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    rtDesc.dwWidth = 64; rtDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE7 rt = nullptr;
    hr = dd7->CreateSurface(&rtDesc, &rt, nullptr);
    PrintHr("CreateSurface(rt, v7)", hr);

    // --- Test B: combined depth+stencil Z-buffer surface, same shape DX6-0 already proved.
    DDSURFACEDESC2 zDesc; memset(&zDesc, 0, sizeof(zDesc));
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    zDesc.dwWidth = 64; zDesc.dwHeight = 64;
    zDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    zDesc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER | DDPF_STENCILBUFFER;
    zDesc.ddpfPixelFormat.dwZBufferBitDepth = 32;
    zDesc.ddpfPixelFormat.dwStencilBitDepth = 8;
    LPDIRECTDRAWSURFACE7 zbuf = nullptr;
    hr = dd7->CreateSurface(&zDesc, &zbuf, nullptr);
    PrintHr("CreateSurface(zbuf, D24S8-style depth+stencil, v7)", hr);
    if (FAILED(hr)) { printf("DX7-SPIKE1-ABORT: no combined depth+stencil surface\n"); return 1; }
    hr = rt->AddAttachedSurface(zbuf);
    PrintHr("AddAttachedSurface(zbuf)", hr);

    LPDIRECT3D7 d3d7 = nullptr;
    hr = dd7->QueryInterface(IID_IDirect3D7, (void**)&d3d7);
    PrintHr("QueryInterface(IID_IDirect3D7)", hr);

    printf("EnumDevices7 (informational -- which device classes Wine offers):\n");
    d3d7->EnumDevices(EnumDevicesCallback, nullptr);

    // --- Test C: CreateDevice with NO trailing outer param (a real signature change vs DX5/DX6's
    // IDirect3D3::CreateDevice), same IID_IDirect3DRGBDevice choice DX2..DX6 already use (a real
    // hardware/TnLHal device may not exist in this headless/Xvfb Wine sandbox at all).
    LPDIRECT3DDEVICE7 device = nullptr;
    hr = d3d7->CreateDevice(IID_IDirect3DRGBDevice, rt, &device);
    PrintHr("C: CreateDevice(RGBDevice7, no outer param, stencil-capable target)", hr);
    if (FAILED(hr)) { printf("DX7-SPIKE1-ABORT: no device with stencil zbuffer\n"); return 1; }

    // --- Test D: NO viewport object at all -- SetViewport(D3DVIEWPORT7) directly on the device.
    D3DVIEWPORT7 vp; memset(&vp, 0, sizeof(vp));
    vp.dwX = 0; vp.dwY = 0; vp.dwWidth = 64; vp.dwHeight = 64;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    hr = device->SetViewport(&vp);
    PrintHr("D: device->SetViewport(D3DVIEWPORT7) -- no viewport object exists at all", hr);

    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);

    // --- Test E: device-direct Clear() (no viewport object to call Clear2 on at all) -- clears
    // color=black, z=1.0, stencil=0 in one call, then a real stencil WRITE pass (left half only).
    D3DRECT fullRect{0, 0, 64, 64};
    hr = device->Clear(1, &fullRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
    PrintHr("E: device->Clear(target=black, z=1.0, stencil=0) -- device-direct, no viewport", hr);

    device->SetRenderState(D3DRENDERSTATE_STENCILENABLE, TRUE);
    device->SetRenderState(D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS);
    device->SetRenderState(D3DRENDERSTATE_STENCILREF, 1);
    device->SetRenderState(D3DRENDERSTATE_STENCILMASK, 0xFFFFFFFF);
    device->SetRenderState(D3DRENDERSTATE_STENCILWRITEMASK, 0xFFFFFFFF);
    device->SetRenderState(D3DRENDERSTATE_STENCILFAIL, D3DSTENCILOP_KEEP);
    device->SetRenderState(D3DRENDERSTATE_STENCILZFAIL, D3DSTENCILOP_KEEP);
    device->SetRenderState(D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_REPLACE);

    D3DTLVERTEX leftQuad[4];
    memset(leftQuad, 0, sizeof(leftQuad));
    for (int i = 0; i < 4; ++i) { leftQuad[i].color = 0xFF00FF00u; leftQuad[i].rhw = 1.0f; leftQuad[i].sz = 0.5f; }
    leftQuad[0].sx = 0;  leftQuad[0].sy = 0;
    leftQuad[1].sx = 32; leftQuad[1].sy = 0;
    leftQuad[2].sx = 32; leftQuad[2].sy = 64;
    leftQuad[3].sx = 0;  leftQuad[3].sy = 64;
    WORD quadIdx[6] = {0, 1, 2, 0, 2, 3};
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, leftQuad, 4, quadIdx, 6, 0);
    PrintHr("DrawIndexedPrimitive(stencil-write pass, left half, green)", hr);
    hr = device->EndScene();
    SamplePixel(rt, 16, 32, "E-left-after-write (expect green, stencil now 1 there)");
    SamplePixel(rt, 48, 32, "E-right-after-write (expect black, stencil still 0 there)");

    // --- Test F: real stencil TEST pass -- a FULL-SCREEN red quad with STENCILFUNC=EQUAL,
    // STENCILREF=1: should only be visible where the stencil buffer already reads 1 (the left half
    // from Test E) -- the right half (stencil=0) must reject this draw and stay black.
    device->SetRenderState(D3DRENDERSTATE_STENCILFUNC, D3DCMP_EQUAL);
    device->SetRenderState(D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP);
    D3DTLVERTEX fullQuad[4];
    memset(fullQuad, 0, sizeof(fullQuad));
    for (int i = 0; i < 4; ++i) { fullQuad[i].color = 0xFFFF0000u; fullQuad[i].rhw = 1.0f; fullQuad[i].sz = 0.5f; }
    fullQuad[0].sx = 0;  fullQuad[0].sy = 0;
    fullQuad[1].sx = 64; fullQuad[1].sy = 0;
    fullQuad[2].sx = 64; fullQuad[2].sy = 64;
    fullQuad[3].sx = 0;  fullQuad[3].sy = 64;
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, fullQuad, 4, quadIdx, 6, 0);
    PrintHr("DrawIndexedPrimitive(stencil-test pass, full screen, red)", hr);
    hr = device->EndScene();
    SamplePixel(rt, 16, 32, "F-left-after-test (expect RED -- stencil==1, test passed)");
    SamplePixel(rt, 48, 32, "F-right-after-test (expect BLACK -- stencil==0, test rejected)");

    // --- Test G: IDirect3DDevice7::SetTexture(stage, surface) binds a texture DIRECTLY from an
    // IDirectDrawSurface7*, with NO texture-handle indirection at all (no
    // IDirect3DTexture2::GetHandle/D3DRENDERSTATE_TEXTUREHANDLE dance DX2..DX6 all needed) -- a
    // real DX7 API simplification worth confirming actually samples correctly before adopting it in
    // the real backend.
    device->SetRenderState(D3DRENDERSTATE_STENCILENABLE, FALSE);
    DDSURFACEDESC2 texDesc; memset(&texDesc, 0, sizeof(texDesc));
    texDesc.dwSize = sizeof(texDesc);
    texDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    texDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE;
    texDesc.dwWidth = 4; texDesc.dwHeight = 4;
    LPDIRECTDRAWSURFACE7 tex = nullptr;
    hr = dd7->CreateSurface(&texDesc, &tex, nullptr);
    PrintHr("G: CreateSurface(4x4 texture, v7)", hr);

    DDSURFACEDESC2 lockedDesc; memset(&lockedDesc, 0, sizeof(lockedDesc));
    lockedDesc.dwSize = sizeof(lockedDesc);
    hr = tex->Lock(nullptr, &lockedDesc, DDLOCK_WAIT, nullptr);
    PrintHr("G: tex->Lock", hr);
    if (SUCCEEDED(hr)) {
        for (int y = 0; y < 4; ++y) {
            auto* row = (uint8_t*)lockedDesc.lpSurface + y * lockedDesc.lPitch;
            for (int x = 0; x < 4; ++x) {
                // Native BGRA byte order (matches this backend family's own DetectChannelLayout
                // default) -- this fills a solid RED (255,0,0) 4x4 texture.
                row[x * 4 + 0] = 0;   // B
                row[x * 4 + 1] = 0;   // G
                row[x * 4 + 2] = 255; // R
                row[x * 4 + 3] = 255; // A
            }
        }
        tex->Unlock(nullptr);
    }

    hr = device->SetTexture(0, tex);
    PrintHr("G: device->SetTexture(0, tex) -- direct surface bind, no handle at all", hr);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    device->Clear(1, &fullRect, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);

    D3DTLVERTEX texQuad[4];
    memset(texQuad, 0, sizeof(texQuad));
    for (int i = 0; i < 4; ++i) { texQuad[i].color = 0xFFFFFFFFu; texQuad[i].rhw = 1.0f; texQuad[i].sz = 0.5f; }
    texQuad[0].sx = 0;  texQuad[0].sy = 0;  texQuad[0].tu = 0.0f; texQuad[0].tv = 0.0f;
    texQuad[1].sx = 64; texQuad[1].sy = 0;  texQuad[1].tu = 1.0f; texQuad[1].tv = 0.0f;
    texQuad[2].sx = 64; texQuad[2].sy = 64; texQuad[2].tu = 1.0f; texQuad[2].tv = 1.0f;
    texQuad[3].sx = 0;  texQuad[3].sy = 64; texQuad[3].tu = 0.0f; texQuad[3].tv = 1.0f;
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, texQuad, 4, quadIdx, 6, 0);
    PrintHr("G: DrawIndexedPrimitive(textured quad via direct SetTexture)", hr);
    hr = device->EndScene();
    SamplePixel(rt, 32, 32, "G-center (expect solid RED (255,0,0) if SetTexture sampled correctly)");

    printf("DX7-SPIKE1-DONE\n");
    return 0;
}
