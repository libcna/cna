// DX6-0 existence-gate spike: confirm real stencil buffer operations work in this environment's
// Wine, the one genuinely new capability plans/plan_dxold.md's roadmap assigns to DX6 that DX2/DX3/DX5
// have all explicitly documented as "no real stencil buffer exists at this DirectX era (DX6+)".
//
// Unlike DX2->DX30->DX5's progression, DX6 introduces NO new COM interface revision at all --
// IDirect3D3/IDirect3DDevice3/IDirect3DViewport3 (confirmed by inspecting the real MinGW headers:
// no IDirect3D4/IDirect3DDevice4 exists) are the exact same interfaces DX5 already uses. DX6's own
// delta is purely new render states/capabilities on top of the SAME interface: stencil ops
// (D3DRENDERSTATE_STENCILENABLE/STENCILFUNC/STENCILFAIL/STENCILZFAIL/STENCILPASS/STENCILREF/
// STENCILMASK/STENCILWRITEMASK, all confirmed present in d3dtypes.h), multitexturing
// (SetTextureStageState/D3DTSS_*, also confirmed present), and DXTn compression (not spiked here --
// see plans/plan_dx6.md for why).
//
// This spike tests ONLY stencil (the primary DX6 deliverable for this plan): a combined
// depth+stencil Z-buffer surface (DDPF_ZBUFFER|DDPF_STENCILBUFFER, 32 bits total: 24 depth + 8
// stencil -- the historically standard D3DFMT_D24S8-equivalent shape) is created, cleared via
// Clear2's own stencil parameter, then a real stencil WRITE (via STENCILPASS=REPLACE) followed by
// a real stencil TEST (via STENCILFUNC=EQUAL) is verified to produce genuinely different pixel
// output depending on where the stencil write landed -- not just "the API calls don't fail."
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
static void SamplePixel(LPDIRECTDRAWSURFACE4 surf, int x, int y, const char* label) {
    DDSURFACEDESC2 d; memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    HRESULT hr = surf->Lock(nullptr, &d, DDLOCK_WAIT, nullptr);
    if (FAILED(hr)) { printf("%s: lock failed hr=0x%08lx\n", label, (unsigned long)hr); return; }
    auto* p = (uint8_t*)d.lpSurface + y * d.lPitch + x * 4;
    printf("%s (%d,%d) = (%3d,%3d,%3d)\n", label, x, y, p[2], p[1], p[0]);
    surf->Unlock(nullptr);
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx6Spike1Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx6Spike1Window", L"DX6 Spike1",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    LPDIRECTDRAW ddV1 = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &ddV1, nullptr);
    PrintHr("DirectDrawCreate (v1)", hr);
    hr = ddV1->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    PrintHr("SetCooperativeLevel", hr);

    LPDIRECTDRAW4 dd4 = nullptr;
    hr = ddV1->QueryInterface(IID_IDirectDraw4, (void**)&dd4);
    PrintHr("QueryInterface(IID_IDirectDraw4)", hr);

    DDSURFACEDESC2 rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
    rtDesc.dwSize = sizeof(rtDesc);
    rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    rtDesc.dwWidth = 64; rtDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE4 rt = nullptr;
    hr = dd4->CreateSurface(&rtDesc, &rt, nullptr);
    PrintHr("CreateSurface(rt)", hr);

    // --- Test A: combined depth+stencil Z-buffer surface (32-bit total: 24 depth + 8 stencil).
    DDSURFACEDESC2 zDesc; memset(&zDesc, 0, sizeof(zDesc));
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    zDesc.dwWidth = 64; zDesc.dwHeight = 64;
    zDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    zDesc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER | DDPF_STENCILBUFFER;
    zDesc.ddpfPixelFormat.dwZBufferBitDepth = 32;
    zDesc.ddpfPixelFormat.dwStencilBitDepth = 8;
    LPDIRECTDRAWSURFACE4 zbuf = nullptr;
    hr = dd4->CreateSurface(&zDesc, &zbuf, nullptr);
    PrintHr("CreateSurface(zbuf, D24S8-style depth+stencil)", hr);
    if (FAILED(hr)) { printf("DX6-SPIKE1-ABORT: no combined depth+stencil surface\n"); return 1; }
    hr = rt->AddAttachedSurface(zbuf);
    PrintHr("AddAttachedSurface(zbuf)", hr);

    LPDIRECT3D3 d3d3 = nullptr;
    hr = dd4->QueryInterface(IID_IDirect3D3, (void**)&d3d3);
    PrintHr("QueryInterface(IID_IDirect3D3)", hr);
    LPDIRECT3DDEVICE3 device = nullptr;
    hr = d3d3->CreateDevice(IID_IDirect3DRGBDevice, rt, &device, nullptr);
    PrintHr("CreateDevice(RGBDevice3, stencil-capable target)", hr);
    if (FAILED(hr)) { printf("DX6-SPIKE1-ABORT: no device with stencil zbuffer\n"); return 1; }

    LPDIRECT3DVIEWPORT3 viewport = nullptr;
    d3d3->CreateViewport(&viewport, nullptr);
    device->AddViewport(viewport);
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

    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);

    // --- Test B: Clear2 with an explicit stencil value (0), then a real stencil WRITE pass:
    // draw a quad covering only the LEFT HALF, STENCILFUNC=ALWAYS (always passes),
    // STENCILPASS=REPLACE, STENCILREF=1 -- this should leave stencil=1 under the left half only,
    // stencil=0 (from Clear2) under the right half.
    D3DRECT fullRect{0, 0, 64, 64};
    hr = viewport->Clear2(1, &fullRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
    PrintHr("Clear2(target=black, z=1.0, stencil=0)", hr);

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
    SamplePixel(rt, 16, 32, "B-left-after-write (expect green, stencil now 1 there)");
    SamplePixel(rt, 48, 32, "B-right-after-write (expect black, stencil still 0 there)");

    // --- Test C: real stencil TEST pass -- a FULL-SCREEN red quad with STENCILFUNC=EQUAL,
    // STENCILREF=1: should only be visible where the stencil buffer already reads 1 (the left
    // half from Test B) -- the right half (stencil=0) must reject this draw and show whatever was
    // there before (black).
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
    SamplePixel(rt, 16, 32, "C-left-after-test (expect RED -- stencil==1, test passed)");
    SamplePixel(rt, 48, 32, "C-right-after-test (expect BLACK -- stencil==0, test rejected)");

    printf("DX6-SPIKE1-DONE\n");
    return 0;
}
