// DX2-0-followup spike (plans/plan_dx2.md Phase O9): validate three pieces of IDirect3DDevice2 surface
// this backend has NOT yet exercised, needed to decide Phase O9's scope honestly rather than
// assume:
//   Test C: D3DRENDERSTATE_SPECULARENABLE + D3DTLVERTEX::specular -- the real fixed-function
//           mechanism for "add a Gouraud specular highlight AFTER the texture-modulate stage",
//           which is exactly what CPU-side BasicEffect-style lighting needs to reproduce
//           real XNA's `color.rgb += specular*color.a` (Lighting.fxh's AddSpecular) using genuine
//           Direct3D hardware compositing instead of CPU-blending it into the diffuse channel.
//   Test D: D3DRENDERSTATE_FILLMODE = D3DFILL_WIREFRAME vs D3DFILL_SOLID -- does wireframe mode
//           actually produce edge-only output on this environment's software RGB device, or does
//           it silently render solid-filled anyway?
//   Test E: D3DRENDERSTATE_ANISOTROPY (min filter D3DTFN_ANISOTROPIC) vs D3DTFN_LINEAR/POINT on a
//           heavily-minified checkerboard texture -- does anisotropic filtering actually produce
//           different sampled output on this software rasterizer, or is it a silent no-op?
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
static void SamplePixel(LPDIRECTDRAWSURFACE surf, int x, int y, const char* label) {
    DDSURFACEDESC d; memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    HRESULT hr = surf->Lock(nullptr, &d, DDLOCK_WAIT, nullptr);
    if (FAILED(hr)) { printf("%s: lock failed\n", label); return; }
    auto* p = (uint8_t*)d.lpSurface + y * d.lPitch + x * 4;
    printf("%s (%d,%d) = (%3d,%3d,%3d)\n", label, x, y, p[2], p[1], p[0]);
    surf->Unlock(d.lpSurface);
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx2Spike10Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike10Window", L"DX2 Spike10",
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

    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);

    D3DRECT clearRect{0, 0, 64, 64};

    // --- Test C: SPECULARENABLE + D3DTLVERTEX::specular, on top of a BLACK-modulated diffuse
    // channel, so any non-black readback can only have come from the specular-add path.
    viewport->Clear(1, &clearRect, D3DCLEAR_TARGET);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, 0);
    device->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    D3DTLVERTEX specVerts[4];
    memset(specVerts, 0, sizeof(specVerts));
    // diffuse color BLACK (so the base/diffuse pass contributes nothing), specular color RED.
    for (int i = 0; i < 4; ++i) { specVerts[i].color = 0xFF000000u; specVerts[i].specular = 0xFFFF0000u; specVerts[i].rhw = 1.0f; }
    specVerts[0].sx = 0;  specVerts[0].sy = 0;  specVerts[0].sz = 0.5f;
    specVerts[1].sx = 64; specVerts[1].sy = 0;  specVerts[1].sz = 0.5f;
    specVerts[2].sx = 64; specVerts[2].sy = 64; specVerts[2].sz = 0.5f;
    specVerts[3].sx = 0;  specVerts[3].sy = 64; specVerts[3].sz = 0.5f;
    WORD quadIdx[6] = {0,1,2, 0,2,3};
    hr = device->BeginScene(); PrintHr("BeginScene(C)", hr);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, specVerts, 4, quadIdx, 6, 0);
    PrintHr("DrawIndexedPrimitive(C specular-enabled)", hr);
    hr = device->EndScene(); PrintHr("EndScene(C)", hr);
    SamplePixel(rt, 32, 32, "C(specular-enabled, expect red if additive)");

    viewport->Clear(1, &clearRect, D3DCLEAR_TARGET);
    device->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    hr = device->BeginScene(); PrintHr("BeginScene(C2)", hr);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, specVerts, 4, quadIdx, 6, 0);
    PrintHr("DrawIndexedPrimitive(C2 specular-disabled)", hr);
    hr = device->EndScene(); PrintHr("EndScene(C2)", hr);
    SamplePixel(rt, 32, 32, "C2(specular-disabled, expect black -- confirms C wasn't just diffuse)");
    device->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);

    // --- Test D: FILLMODE wireframe vs solid. Triangle covering the center; sample the centroid
    // (filled in SOLID, background in WIREFRAME) and a point ON an edge (drawn in both modes).
    D3DTLVERTEX triVerts[3];
    memset(triVerts, 0, sizeof(triVerts));
    triVerts[0].sx = 8;  triVerts[0].sy = 8;  triVerts[0].sz = 0.5f; triVerts[0].rhw = 1.0f; triVerts[0].color = 0xFFFFFFFFu;
    triVerts[1].sx = 56; triVerts[1].sy = 8;  triVerts[1].sz = 0.5f; triVerts[1].rhw = 1.0f; triVerts[1].color = 0xFFFFFFFFu;
    triVerts[2].sx = 32; triVerts[2].sy = 56; triVerts[2].sz = 0.5f; triVerts[2].rhw = 1.0f; triVerts[2].color = 0xFFFFFFFFu;

    viewport->Clear(1, &clearRect, D3DCLEAR_TARGET); // clears to black
    device->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
    hr = device->BeginScene(); PrintHr("BeginScene(D-solid)", hr);
    hr = device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, triVerts, 3, 0);
    PrintHr("DrawPrimitive(D-solid)", hr);
    hr = device->EndScene(); PrintHr("EndScene(D-solid)", hr);
    SamplePixel(rt, 32, 24, "D-solid centroid-ish (expect white, filled)");

    viewport->Clear(1, &clearRect, D3DCLEAR_TARGET);
    device->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_WIREFRAME);
    hr = device->BeginScene(); PrintHr("BeginScene(D-wire)", hr);
    hr = device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, triVerts, 3, 0);
    PrintHr("DrawPrimitive(D-wire)", hr);
    hr = device->EndScene(); PrintHr("EndScene(D-wire)", hr);
    SamplePixel(rt, 32, 24, "D-wire same point (expect BLACK if wireframe genuinely hollow)");
    device->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);

    // --- Test E: anisotropic vs linear vs point minification filtering on a heavily-minified
    // high-frequency checkerboard texture (8x8 checker into a tiny on-screen quad), sampled at an
    // oblique-ish angle via UV gradient (approximated here by just heavy minification, since this
    // backend has no per-pixel derivative control anyway -- same simplification the rest of this
    // spike family uses).
    constexpr int kTexN = 8;
    DDSURFACEDESC texDesc; memset(&texDesc, 0, sizeof(texDesc));
    texDesc.dwSize = sizeof(texDesc);
    texDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    texDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    texDesc.dwWidth = kTexN; texDesc.dwHeight = kTexN;
    texDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    texDesc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    texDesc.ddpfPixelFormat.dwRGBBitCount = 32;
    texDesc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
    texDesc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
    texDesc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
    texDesc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
    LPDIRECTDRAWSURFACE texSurf = nullptr;
    hr = dd->CreateSurface(&texDesc, &texSurf, nullptr);
    PrintHr("CreateSurface(checker-tex)", hr);
    DDSURFACEDESC texLock; memset(&texLock, 0, sizeof(texLock));
    texLock.dwSize = sizeof(texLock);
    hr = texSurf->Lock(nullptr, &texLock, DDLOCK_WAIT, nullptr);
    if (SUCCEEDED(hr)) {
        for (int y = 0; y < kTexN; ++y) {
            auto* row = (uint32_t*)((uint8_t*)texLock.lpSurface + y * texLock.lPitch);
            for (int x = 0; x < kTexN; ++x)
                row[x] = ((x + y) % 2 == 0) ? 0xFFFFFFFFu : 0xFF000000u;
        }
        texSurf->Unlock(texLock.lpSurface);
    }
    LPDIRECT3DTEXTURE2 tex2 = nullptr;
    hr = texSurf->QueryInterface(IID_IDirect3DTexture2, (void**)&tex2);
    PrintHr("QueryInterface(IDirect3DTexture2 checker)", hr);
    D3DTEXTUREHANDLE texHandle = 0;
    hr = tex2->GetHandle(device, &texHandle);
    PrintHr("GetHandle(checker)", hr);

    device->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, texHandle);
    device->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATE);

    // Minified quad: whole 8x8 checker crammed into a 64x64 target with UV 0..1 across it --
    // not actually minified in screen-space terms since the surface itself is small, so instead
    // shrink the UV range covered by the quad's own texture coords isn't how minification works
    // either. Real minification needs sourceTexels > destPixels; approximate it by sampling the
    // 8x8 texture across a FULL 64x64 quad but requesting a much higher mip/aniso level than the
    // texture even has, to specifically check whether ANISOTROPY/MIN filter enum selection alone
    // changes the sampled result on this device (the actual question Phase O9 needs answered).
    D3DTLVERTEX aTexVerts[4];
    memset(aTexVerts, 0, sizeof(aTexVerts));
    for (int i = 0; i < 4; ++i) { aTexVerts[i].color = 0xFFFFFFFFu; aTexVerts[i].rhw = 1.0f; aTexVerts[i].sz = 0.5f; }
    aTexVerts[0].sx = 0;  aTexVerts[0].sy = 0;  aTexVerts[0].tu = 0.0f; aTexVerts[0].tv = 0.0f;
    aTexVerts[1].sx = 64; aTexVerts[1].sy = 0;  aTexVerts[1].tu = 4.0f; aTexVerts[1].tv = 0.0f;
    aTexVerts[2].sx = 64; aTexVerts[2].sy = 64; aTexVerts[2].tu = 4.0f; aTexVerts[2].tv = 4.0f;
    aTexVerts[3].sx = 0;  aTexVerts[3].sy = 64; aTexVerts[3].tu = 0.0f; aTexVerts[3].tv = 4.0f;

    const auto renderWithFilter = [&](DWORD minFilter, DWORD anisotropy, const char* label) {
        viewport->Clear(1, &clearRect, D3DCLEAR_TARGET);
        device->SetRenderState(D3DRENDERSTATE_TEXTUREMIN, minFilter);
        if (anisotropy > 0) device->SetRenderState(D3DRENDERSTATE_ANISOTROPY, anisotropy);
        device->BeginScene();
        device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, aTexVerts, 4, 0);
        device->EndScene();
        // sample several points across the minified checker to catch any AA/blur difference
        DDSURFACEDESC d; memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        HRESULT lhr = rt->Lock(nullptr, &d, DDLOCK_WAIT, nullptr);
        if (FAILED(lhr)) { printf("%s: lock failed\n", label); return; }
        printf("%s:", label);
        for (int y = 4; y < 64; y += 12) {
            for (int x = 4; x < 64; x += 12) {
                auto* p = (uint8_t*)d.lpSurface + y * d.lPitch + x * 4;
                printf(" %02x%02x%02x", p[2], p[1], p[0]);
            }
        }
        printf("\n");
        rt->Unlock(d.lpSurface);
    };
    renderWithFilter(D3DTFN_POINT, 0, "E-point");
    renderWithFilter(D3DTFN_LINEAR, 0, "E-linear");
    renderWithFilter(D3DTFN_ANISOTROPIC, 4, "E-aniso4");
    renderWithFilter(D3DTFN_ANISOTROPIC, 16, "E-aniso16");

    printf("SPIKE10-DONE\n");
    return 0;
}
