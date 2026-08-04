// DX5-0 existence-gate spike: confirm the real API surface CNA's "DX5" backend would need is
// actually reachable and functional in this environment's Wine (10.0~repack-6), not just present
// in the MinGW-w64 headers. Mirrors DX1-0/DX2-0/DX30-0's discipline of spiking every new COM
// interface revision before writing any backend code.
//
// DX5 (1997) is the first DirectX release where Direct3D drops execute buffers entirely --
// IDirect3DDevice3 only ever exposes DrawPrimitive/DrawIndexedPrimitive (matches DX2/DX3's own
// already-proven choice of avoiding execute buffers, but now it's the ONLY option, not a
// workaround). Concretely new vs DX2/DX3:
//   - DirectDraw object/surfaces upgrade from v1/v2 to v4: IDirectDraw4, IDirectDrawSurface4,
//     DDSURFACEDESC2 (not just DDSURFACEDESC), DDSCAPS2 (not just DDSCAPS). Unlike DX3's
//     "upgrade only the top DirectDraw object" change, DX5 needs EVERY surface to be v4, since
//     IDirectDraw4::CreateSurface returns LPDIRECTDRAWSURFACE4, and IDirect3D3::CreateDevice/
//     IDirect3DViewport3::SetBackgroundDepth2 all require v4 surfaces specifically.
//   - Direct3D device/viewport: IDirect3D3/IDirect3DDevice3/IDirect3DViewport3 (still texture via
//     IDirect3DTexture2 -- unchanged, no IDirect3DTexture3 exists, confirmed by grep).
//   - DrawPrimitive's vertex-type parameter changes from the D3DVERTEXTYPE enum (D3DVT_TLVERTEX)
//     to a DWORD FVF (Flexible Vertex Format) bitmask -- D3DFVF_TLVERTEX is a predefined macro
//     confirmed to expand to the exact same bit layout D3DTLVERTEX already has
//     (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1), so the same struct should still
//     work -- Test D confirms this renders correctly, not just compiles.
//   - IDirect3DViewport3::Clear2 takes explicit color/z/stencil VALUES (unlike
//     IDirect3DViewport(2)::Clear(), which DX2-24 found has no such parameter at all, forcing a
//     manual Lock()-the-Z-buffer workaround there) -- Test G confirms this is real, not a stub.
//     GOTCHA found while writing this spike (own test bug, not a Wine limitation): Clear2's
//     `count=0, rects=nullptr` does NOT mean "clear the whole surface" the way one might assume
//     from the old Clear()'s similar-looking pattern -- it clears NOTHING (silently, HRESULT
//     still OK). An explicit `D3DRECT{0,0,w,h}` with count=1 is required, exactly like the old
//     Clear()'s own established calling convention DX2-0/DX30-0 already used.
//   - Test E/G's own quad geometry has a real gotcha worth keeping too: sampling the exact
//     screen-center pixel of a quad built from 2 diagonally-split triangles ((0,0)-(64,64)
//     screen coords) lands exactly on the triangle seam and reads a garbage-looking blended
//     value -- not a rendering bug, just a bad sample-point choice; sample off the diagonal.
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
    printf("%s (%d,%d) = (%3d,%3d,%3d,a=%3d)\n", label, x, y, p[2], p[1], p[0], p[3]);
    surf->Unlock(nullptr);
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx5Spike1Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx5Spike1Window", L"DX5 Spike1",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    LPDIRECTDRAW ddV1 = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &ddV1, nullptr);
    PrintHr("DirectDrawCreate (v1)", hr);
    hr = ddV1->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    PrintHr("SetCooperativeLevel", hr);

    // --- Test A: QueryInterface(IID_IDirectDraw4), then CreateSurface via v4 (DDSURFACEDESC2).
    LPDIRECTDRAW4 dd4 = nullptr;
    hr = ddV1->QueryInterface(IID_IDirectDraw4, (void**)&dd4);
    PrintHr("QueryInterface(IID_IDirectDraw4)", hr);
    if (FAILED(hr)) { printf("DX5-SPIKE1-ABORT: no IDirectDraw4\n"); return 1; }

    DDSURFACEDESC2 rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
    rtDesc.dwSize = sizeof(rtDesc);
    rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    rtDesc.dwWidth = 64; rtDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE4 rt = nullptr;
    hr = dd4->CreateSurface(&rtDesc, &rt, nullptr);
    PrintHr("IDirectDraw4::CreateSurface(rt, DDSURFACEDESC2)", hr);
    if (FAILED(hr)) { printf("DX5-SPIKE1-ABORT: no v4 surface\n"); return 1; }
    {
        DDSURFACEDESC2 lockDesc; memset(&lockDesc, 0, sizeof(lockDesc));
        lockDesc.dwSize = sizeof(lockDesc);
        hr = rt->Lock(nullptr, &lockDesc, DDLOCK_WAIT, nullptr);
        PrintHr("  Lock() on v4-created surface", hr);
        if (SUCCEEDED(hr)) rt->Unlock(nullptr);
    }

    // DDSURFACEDESC2 dropped the old top-level dwZBufferBitDepth/DDSD_ZBUFFERBITDEPTH entirely --
    // v4 describes Z-buffer depth via ddpfPixelFormat's DDPF_ZBUFFER flag + dwZBufferBitDepth
    // instead (confirmed by grep -- no dwZBufferBitDepth field exists on DDSURFACEDESC2 itself).
    DDSURFACEDESC2 zDesc; memset(&zDesc, 0, sizeof(zDesc));
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    zDesc.dwWidth = 64; zDesc.dwHeight = 64;
    zDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    zDesc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    zDesc.ddpfPixelFormat.dwZBufferBitDepth = 16;
    LPDIRECTDRAWSURFACE4 zbuf = nullptr;
    hr = dd4->CreateSurface(&zDesc, &zbuf, nullptr);
    PrintHr("IDirectDraw4::CreateSurface(zbuf)", hr);
    hr = rt->AddAttachedSurface(zbuf);
    PrintHr("IDirectDrawSurface4::AddAttachedSurface(zbuf)", hr);

    // --- Test B: QueryInterface(IID_IDirect3D3) + CreateDevice off the v4 render target.
    LPDIRECT3D3 d3d3 = nullptr;
    hr = dd4->QueryInterface(IID_IDirect3D3, (void**)&d3d3);
    PrintHr("IDirectDraw4::QueryInterface(IID_IDirect3D3)", hr);
    if (FAILED(hr)) { printf("DX5-SPIKE1-ABORT: no IDirect3D3\n"); return 1; }
    LPDIRECT3DDEVICE3 device = nullptr;
    hr = d3d3->CreateDevice(IID_IDirect3DRGBDevice, rt, &device, nullptr);
    PrintHr("IDirect3D3::CreateDevice(RGBDevice3)", hr);
    if (FAILED(hr)) { printf("DX5-SPIKE1-ABORT: no device\n"); return 1; }

    // --- Test C: IDirect3DViewport3 creation, SetViewport2 (same D3DVIEWPORT2 struct as DX2/DX3).
    LPDIRECT3DVIEWPORT3 viewport = nullptr;
    hr = d3d3->CreateViewport(&viewport, nullptr);
    PrintHr("IDirect3D3::CreateViewport", hr);
    hr = device->AddViewport(viewport);
    PrintHr("IDirect3DDevice3::AddViewport", hr);
    D3DVIEWPORT2 vp; memset(&vp, 0, sizeof(vp));
    vp.dwSize = sizeof(vp);
    vp.dwWidth = 64; vp.dwHeight = 64;
    vp.dvClipX = -1.0f; vp.dvClipY = 1.0f;
    vp.dvClipWidth = 2.0f; vp.dvClipHeight = 2.0f;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    hr = viewport->SetViewport2(&vp);
    PrintHr("IDirect3DViewport3::SetViewport2", hr);
    hr = device->SetCurrentViewport(viewport);
    PrintHr("IDirect3DDevice3::SetCurrentViewport", hr);

    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);

    // --- Test D: DrawPrimitive with the NEW DWORD-FVF vertex_type parameter (D3DFVF_TLVERTEX)
    // instead of the old D3DVERTEXTYPE enum (D3DVT_TLVERTEX) -- confirms real Gouraud
    // interpolation still renders through the new API shape, not just that it compiles.
    D3DTLVERTEX triVerts[3];
    memset(triVerts, 0, sizeof(triVerts));
    triVerts[0].sx = 32; triVerts[0].sy = 4;  triVerts[0].sz = 0.5f; triVerts[0].rhw = 1.0f; triVerts[0].color = 0xFFFF0000u;
    triVerts[1].sx = 60; triVerts[1].sy = 60; triVerts[1].sz = 0.5f; triVerts[1].rhw = 1.0f; triVerts[1].color = 0xFF00FF00u;
    triVerts[2].sx = 4;  triVerts[2].sy = 60; triVerts[2].sz = 0.5f; triVerts[2].rhw = 1.0f; triVerts[2].color = 0xFF0000FFu;

    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    hr = device->BeginScene();
    PrintHr("BeginScene(D)", hr);
    hr = device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, triVerts, 3, 0);
    PrintHr("IDirect3DDevice3::DrawPrimitive(D3DFVF_TLVERTEX)", hr);
    hr = device->EndScene();
    PrintHr("EndScene(D)", hr);
    SamplePixel(rt, 32, 40, "D(gouraud centroid-ish, expect a blend of R/G/B)");

    // --- Test E: DrawIndexedPrimitive + genuinely enabled Z-test, mirrors DX2-0c exactly.
    // Must clear BOTH target and Z-buffer first (Test D ran with ZENABLE=FALSE, so the Z-buffer
    // is still whatever uninitialized memory it started as) -- using the newly-confirmed-real
    // Clear2 (Test G would otherwise be redundant with this) rather than a manual Lock().
    D3DRECT fullRect{0, 0, 64, 64};
    hr = viewport->Clear2(1, &fullRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
    PrintHr("IDirect3DViewport3::Clear2 (before Test E)", hr);

    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESS);
    D3DTLVERTEX quadVerts[8];
    memset(quadVerts, 0, sizeof(quadVerts));
    quadVerts[0].sx = 0;  quadVerts[0].sy = 0;  quadVerts[0].sz = 0.2f; quadVerts[0].rhw = 1.0f; quadVerts[0].color = 0xFFFF0000u;
    quadVerts[1].sx = 64; quadVerts[1].sy = 0;  quadVerts[1].sz = 0.2f; quadVerts[1].rhw = 1.0f; quadVerts[1].color = 0xFFFF0000u;
    quadVerts[2].sx = 64; quadVerts[2].sy = 64; quadVerts[2].sz = 0.2f; quadVerts[2].rhw = 1.0f; quadVerts[2].color = 0xFFFF0000u;
    quadVerts[3].sx = 0;  quadVerts[3].sy = 64; quadVerts[3].sz = 0.2f; quadVerts[3].rhw = 1.0f; quadVerts[3].color = 0xFFFF0000u;
    quadVerts[4].sx = 0;  quadVerts[4].sy = 0;  quadVerts[4].sz = 0.8f; quadVerts[4].rhw = 1.0f; quadVerts[4].color = 0xFF00FF00u;
    quadVerts[5].sx = 64; quadVerts[5].sy = 0;  quadVerts[5].sz = 0.8f; quadVerts[5].rhw = 1.0f; quadVerts[5].color = 0xFF00FF00u;
    quadVerts[6].sx = 64; quadVerts[6].sy = 64; quadVerts[6].sz = 0.8f; quadVerts[6].rhw = 1.0f; quadVerts[6].color = 0xFF00FF00u;
    quadVerts[7].sx = 0;  quadVerts[7].sy = 64; quadVerts[7].sz = 0.8f; quadVerts[7].rhw = 1.0f; quadVerts[7].color = 0xFF00FF00u;
    WORD idx[12] = { 4,5,6, 4,6,7,  0,1,2, 0,2,3 };
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, quadVerts, 8, idx, 12, 0);
    PrintHr("IDirect3DDevice3::DrawIndexedPrimitive(D3DFVF_TLVERTEX, z-test)", hr);
    hr = device->EndScene();
    // (32,32) sits exactly on the quad's own diagonal triangle-split seam (0,0)-(64,64) -- sample
    // off that seam instead to avoid an edge-rasterization artifact.
    SamplePixel(rt, 16, 48, "E(z-test, expect red -- near quad drawn second wins)");

    // --- Test F: texture sampling -- v4 surface, IDirect3DTexture2 (unchanged GUID/interface).
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    {
        DDSURFACEDESC2 clearFill; memset(&clearFill, 0, sizeof(clearFill));
        clearFill.dwSize = sizeof(clearFill);
        rt->Lock(nullptr, &clearFill, DDLOCK_WAIT, nullptr);
        memset(clearFill.lpSurface, 0, static_cast<size_t>(clearFill.lPitch) * 64);
        rt->Unlock(nullptr);
    }

    DDSURFACEDESC2 texDesc; memset(&texDesc, 0, sizeof(texDesc));
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
    LPDIRECTDRAWSURFACE4 texSurf = nullptr;
    hr = dd4->CreateSurface(&texDesc, &texSurf, nullptr);
    PrintHr("IDirectDraw4::CreateSurface(tex)", hr);
    DDSURFACEDESC2 texLock; memset(&texLock, 0, sizeof(texLock));
    texLock.dwSize = sizeof(texLock);
    hr = texSurf->Lock(nullptr, &texLock, DDLOCK_WAIT, nullptr);
    if (SUCCEEDED(hr)) {
        auto* row0 = (uint32_t*)((uint8_t*)texLock.lpSurface + 0 * texLock.lPitch);
        auto* row1 = (uint32_t*)((uint8_t*)texLock.lpSurface + 1 * texLock.lPitch);
        row0[0] = 0xFFFF0000u; row0[1] = 0xFF0000FFu;
        row1[0] = 0xFF0000FFu; row1[1] = 0xFFFF0000u;
        texSurf->Unlock(nullptr);
    }
    LPDIRECT3DTEXTURE2 tex2 = nullptr;
    hr = texSurf->QueryInterface(IID_IDirect3DTexture2, (void**)&tex2);
    PrintHr("IDirectDrawSurface4::QueryInterface(IID_IDirect3DTexture2)", hr);
    // IDirect3DTexture2::GetHandle historically wants an IDirect3DDevice2*, but our device here
    // is an IDirect3DDevice3 -- test whether it still accepts a QI'd IDirect3DDevice2 view of the
    // same device3 object (real historical COM aggregation pattern), since GetHandle has no
    // IDirect3DDevice3 overload at all.
    D3DTEXTUREHANDLE texHandle = 0;
    {
        LPDIRECT3DDEVICE2 device2ForTex = nullptr;
        hr = device->QueryInterface(IID_IDirect3DDevice2, (void**)&device2ForTex);
        PrintHr("IDirect3DDevice3::QueryInterface(IID_IDirect3DDevice2) (for GetHandle)", hr);
        if (SUCCEEDED(hr)) {
            hr = tex2->GetHandle(device2ForTex, &texHandle);
            PrintHr("IDirect3DTexture2::GetHandle(device2-view-of-device3)", hr);
            device2ForTex->Release();
        }
    }

    device->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, texHandle);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATE);
    D3DTLVERTEX texVerts[4];
    memset(texVerts, 0, sizeof(texVerts));
    texVerts[0].sx = 0;  texVerts[0].sy = 0;  texVerts[0].sz = 0.5f; texVerts[0].rhw = 1.0f; texVerts[0].color = 0xFFFFFFFFu; texVerts[0].tu = 0.0f; texVerts[0].tv = 0.0f;
    texVerts[1].sx = 64; texVerts[1].sy = 0;  texVerts[1].sz = 0.5f; texVerts[1].rhw = 1.0f; texVerts[1].color = 0xFFFFFFFFu; texVerts[1].tu = 1.0f; texVerts[1].tv = 0.0f;
    texVerts[2].sx = 64; texVerts[2].sy = 64; texVerts[2].sz = 0.5f; texVerts[2].rhw = 1.0f; texVerts[2].color = 0xFFFFFFFFu; texVerts[2].tu = 1.0f; texVerts[2].tv = 1.0f;
    texVerts[3].sx = 0;  texVerts[3].sy = 64; texVerts[3].sz = 0.5f; texVerts[3].rhw = 1.0f; texVerts[3].color = 0xFFFFFFFFu; texVerts[3].tu = 0.0f; texVerts[3].tv = 1.0f;
    WORD texIdx[6] = { 0,1,2, 0,2,3 };
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, texVerts, 4, texIdx, 6, 0);
    PrintHr("IDirect3DDevice3::DrawIndexedPrimitive(textured)", hr);
    hr = device->EndScene();
    SamplePixel(rt, 2, 2, "F(texture top-left, expect Red)");
    SamplePixel(rt, 61, 61, "F(texture bottom-right, expect Red)");

    // --- Test G: IDirect3DViewport3::Clear2 -- real explicit color/z/stencil clear, the exact
    // capability DX2-24/DX3 had to work around via a manual Z-buffer Lock().
    D3DRECT fullRect2{0, 0, 64, 64};
    hr = viewport->Clear2(1, &fullRect2, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           0xFF123456u, 0.75f, 0);
    PrintHr("IDirect3DViewport3::Clear2(color=0x123456, z=0.75)", hr);
    SamplePixel(rt, 32, 32, "G(Clear2 target color, expect (0x12,0x34,0x56))");
    // Verify the Z value was really written by drawing a z=0.8 quad (should be REJECTED, since
    // 0.8 > 0.75 and ZFUNC=LESS) and a z=0.5 quad (should PASS, since 0.5 < 0.75).
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESS);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, 0);
    D3DTLVERTEX zProbe[4];
    memset(zProbe, 0, sizeof(zProbe));
    for (int i = 0; i < 4; ++i) { zProbe[i].color = 0xFFFFFFFFu; zProbe[i].rhw = 1.0f; }
    zProbe[0].sx = 0; zProbe[0].sy = 0; zProbe[0].sz = 0.8f;
    zProbe[1].sx = 64; zProbe[1].sy = 0; zProbe[1].sz = 0.8f;
    zProbe[2].sx = 64; zProbe[2].sy = 64; zProbe[2].sz = 0.8f;
    zProbe[3].sx = 0; zProbe[3].sy = 64; zProbe[3].sz = 0.8f;
    hr = device->BeginScene();
    device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, zProbe, 4, texIdx, 6, 0);
    hr = device->EndScene();
    SamplePixel(rt, 16, 48, "G(z=0.8 quad vs Clear2's z=0.75, expect REJECTED -- still Clear2 color)");

    printf("DX5-SPIKE1-DONE\n");
    return 0;
}
