// DX8-0 existence-gate spike: confirm real DirectX 8 fixed-function 3D rendering, delivered via
// DXVK (not Wine's own ddraw.dll/d3d8.dll), works in this environment before any backend code is
// written. Per the user's explicit scope decision, DX8 targets FIXED-FUNCTION 3D only (matching
// DX1..DX7's own CPU-transform-and-submit shape), not real Shader Model 1.x programmable shaders --
// docs/directx-legacy-backends-analysis.md already notes real XNA effects need ps_2_0+ regardless,
// so SM1.x buys nothing for CNA's own actual content.
//
// DX8 is architecturally very different from DX1..DX7:
//   1. No DirectDraw at all -- "DirectDraw+Direct3D merged" (plans/plan_dxold.md's own DX8 row). A single
//      IDirect3D8::CreateDevice(adapter, type, hFocusWindow, behaviorFlags, &presentParams, &device)
//      call creates BOTH the device and its own swap chain -- no separate ddraw object, no manual
//      "shadow backbuffer + Blt to primary" trick DX1..DX7 all needed.
//   2. mingw-w64's x86_64 target ships NO real d3d8 import library at all (only libd3d8thk.a, the
//      unrelated internal "thunk" library, and only the i686/32-bit target has a real libd3d8.a) --
//      but DXVK's own /usr/lib/dxvk/wine64/d3d8.dll.a exports the real Direct3DCreate8 symbol, so
//      THAT is what this spike (and the eventual backend) links against directly.
//   3. D3DTLVERTEX/D3DFVF_TLVERTEX (the canned vertex struct DX2..DX7 all used) is GONE from the
//      real d3d8types.h/d3d9types.h headers entirely -- D3D8 introduced the generic FVF/vertex-
//      declaration model, so the caller must define its own struct matching the FVF byte layout.
//      The FVF *values* themselves (D3DFVF_XYZRHW/DIFFUSE/SPECULAR/TEX1) still exist unchanged.
//   4. D3D8's DrawPrimitive/DrawIndexedPrimitive require a bound vertex buffer (SetStreamSource) --
//      the CPU-memory-pointer submission DX2..DX7 all used is DrawPrimitiveUP/DrawIndexedPrimitiveUP
//      instead, which additionally require the FVF to be set via SetVertexShader(fvfValue) FIRST (a
//      real, confusing D3D8-only idiom: passing a raw FVF DWORD, not a real vertex-shader handle, to
//      SetVertexShader -- D3D9 later split this into a separate SetFVF() method). This spike tests
//      whether that idiom actually works, not just compiles.
//   5. No GetRenderTargetData (a D3D9-only addition) -- readback here uses D3D8's own
//      CreateImageSurface (a lockable system-memory surface) + CopyRects from the real back buffer.
//
// This spike tests, in order: (A) Direct3DCreate8+CreateDevice via the DXVK-installed d3d8.dll,
// confirming DXVK (not WineD3D) actually handled it; (B) Clear's rect_count=0/rects=nullptr
// semantics (DX5-DX7's own Clear2 had a real gotcha here -- must verify D3D8's Clear independently,
// not assume it matches either prior convention); (C) SetVertexShader(FVF)+DrawPrimitiveUP rendering
// a Gouraud-shaded triangle; (D) DrawIndexedPrimitiveUP + real Z-test occlusion; (E) a real
// CreateTexture + SetTexture + SetTextureStageState(COLOROP=MODULATE) sample (pre-empting the exact
// DX7-0 finding that the legacy D3DRENDERSTATE_TEXTUREMAPBLEND state might be rejected here too);
// (F) real stencil write+test against a D3DFMT_D24S8 depth-stencil surface.
#include <windows.h>
#include <d3d8.h>
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

// Same byte layout as the old D3DTLVERTEX (which no longer exists in d3d8types.h): 4 floats
// position+rhw, 2 D3DCOLORs, 2 floats UV -- matches D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|
// D3DFVF_TEX1 exactly.
struct Dx8TLVertex {
    float sx, sy, sz, rhw;
    D3DCOLOR color;
    D3DCOLOR specular;
    float tu, tv;
};
static const DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;

static void SamplePixel(IDirect3DDevice8* device, int width, int height, int x, int y, const char* label) {
    IDirect3DSurface8* backBuffer = nullptr;
    HRESULT hr = device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (FAILED(hr)) { printf("%s: GetBackBuffer failed hr=0x%08lx\n", label, (unsigned long)hr); return; }

    IDirect3DSurface8* sysmem = nullptr;
    hr = device->CreateImageSurface(width, height, D3DFMT_A8R8G8B8, &sysmem);
    if (FAILED(hr)) { printf("%s: CreateImageSurface failed hr=0x%08lx\n", label, (unsigned long)hr); backBuffer->Release(); return; }

    hr = device->CopyRects(backBuffer, nullptr, 0, sysmem, nullptr);
    if (FAILED(hr)) { printf("%s: CopyRects failed hr=0x%08lx\n", label, (unsigned long)hr); sysmem->Release(); backBuffer->Release(); return; }

    D3DLOCKED_RECT locked{};
    hr = sysmem->LockRect(&locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) { printf("%s: LockRect failed hr=0x%08lx\n", label, (unsigned long)hr); sysmem->Release(); backBuffer->Release(); return; }

    auto* p = (uint8_t*)locked.pBits + y * locked.Pitch + x * 4;
    printf("%s (%d,%d) = (%3d,%3d,%3d)\n", label, x, y, p[2], p[1], p[0]);

    sysmem->UnlockRect();
    sysmem->Release();
    backBuffer->Release();
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx8Spike1Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx8Spike1Window", L"DX8 Spike1",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // --- Test A: Direct3DCreate8 + CreateDevice, via DXVK's d3d8.dll (installed into this Wine
    // prefix's system32 as a native override, mirroring dxvk-setup's own install_dll steps).
    IDirect3D8* d3d8 = Direct3DCreate8(D3D_SDK_VERSION);
    printf("Direct3DCreate8: %s\n", d3d8 ? "OK (non-null)" : "FAIL (null)");
    if (!d3d8) { printf("DX8-SPIKE1-ABORT: Direct3DCreate8 returned null\n"); return 1; }

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
    // FullScreen_PresentationInterval is documented as meaningful only when Windowed=FALSE;
    // D3DPRESENT_INTERVAL_DEFAULT (0) is the correct value for windowed mode.
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    IDirect3DDevice8* device = nullptr;
    HRESULT hr = d3d8->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    PrintHr("A: IDirect3D8::CreateDevice", hr);
    if (FAILED(hr)) { printf("DX8-SPIKE1-ABORT: CreateDevice failed\n"); return 1; }

    // --- Test B: Clear() with rect_count=0/rects=nullptr -- does this clear the WHOLE target
    // (D3D9's own convention) or silently clear NOTHING (the DX5-DX7 IDirect3DViewport::Clear2
    // gotcha)? Must be verified independently, not assumed either way.
    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        D3DCOLOR_ARGB(255, 10, 10, 10), 1.0f, 0);
    PrintHr("B: Clear(rect_count=0, rects=nullptr, TARGET|ZBUFFER|STENCIL)", hr);
    SamplePixel(device, 64, 64, 32, 32, "B-center (expect dark gray (10,10,10) if count=0 clears everything)");

    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

    // --- Test C: the real D3D8 FVF-via-SetVertexShader idiom -- pass the raw FVF DWORD directly to
    // SetVertexShader (NOT a real vertex-shader handle), then DrawPrimitiveUP with a CPU vertex
    // array (no vertex buffer at all), rendering a Gouraud-shaded 2-triangle quad.
    hr = device->SetVertexShader(kFvf);
    PrintHr("C: SetVertexShader(raw FVF value, not a shader handle)", hr);

    Dx8TLVertex quad[4] = {};
    quad[0] = {0.0f,  0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0.0f, 0.0f};
    quad[1] = {64.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,0,255,0), 0, 1.0f, 0.0f};
    quad[2] = {64.0f, 64.0f,0.5f, 1.0f, D3DCOLOR_ARGB(255,0,0,255), 0, 1.0f, 1.0f};
    quad[3] = {0.0f,  64.0f,0.5f, 1.0f, D3DCOLOR_ARGB(255,255,255,0), 0, 0.0f, 1.0f};
    WORD quadIdx[6] = {0, 1, 2, 0, 2, 3};

    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, quadIdx, D3DFMT_INDEX16, quad, sizeof(Dx8TLVertex));
    PrintHr("C: DrawIndexedPrimitiveUP(Gouraud quad, no vertex buffer)", hr);
    hr = device->EndScene();
    SamplePixel(device, 64, 64, 4, 4, "C-near-red-corner (expect near-red)");
    SamplePixel(device, 64, 64, 60, 60, "C-near-blue-corner (expect near-blue)");

    // --- Test D: real Z-test occlusion -- draw far(blue) then near(red) overlapping triangles,
    // ZENABLE=TRUE, and confirm near wins regardless of draw order (order-independent occlusion).
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255,0,0,0), 1.0f, 0);
    Dx8TLVertex farBlue[3] = {
        {0.0f, 0.0f, 0.8f, 1.0f, D3DCOLOR_ARGB(255,0,0,255), 0, 0, 0},
        {64.0f, 0.0f, 0.8f, 1.0f, D3DCOLOR_ARGB(255,0,0,255), 0, 0, 0},
        {32.0f, 64.0f, 0.8f, 1.0f, D3DCOLOR_ARGB(255,0,0,255), 0, 0, 0},
    };
    Dx8TLVertex nearRed[3] = {
        {0.0f, 0.0f, 0.2f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
        {64.0f, 0.0f, 0.2f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
        {32.0f, 64.0f, 0.2f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
    };
    hr = device->BeginScene();
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, farBlue, sizeof(Dx8TLVertex));
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, nearRed, sizeof(Dx8TLVertex));
    hr = device->EndScene();
    SamplePixel(device, 64, 64, 32, 40, "D-after-far-then-near (expect RED -- near wins)");

    // --- Test E: real texture creation + sampling via SetTexture + SetTextureStageState
    // (pre-empting DX7-0's own real finding that the legacy D3DRENDERSTATE_TEXTUREMAPBLEND render
    // state was rejected outright by that device revision -- using the stage-state mechanism here
    // from the start rather than discovering the same rejection again).
    IDirect3DTexture8* tex = nullptr;
    hr = device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
    PrintHr("E: CreateTexture(4x4)", hr);
    if (tex) {
        D3DLOCKED_RECT lr{};
        hr = tex->LockRect(0, &lr, nullptr, 0);
        if (SUCCEEDED(hr)) {
            for (int y = 0; y < 4; ++y) {
                auto* row = (uint8_t*)lr.pBits + y * lr.Pitch;
                for (int x = 0; x < 4; ++x) {
                    row[x*4+0] = 0; row[x*4+1] = 0; row[x*4+2] = 255; row[x*4+3] = 255; // solid red (BGRA)
                }
            }
            tex->UnlockRect(0);
        }
        hr = device->SetTexture(0, tex);
        PrintHr("E: SetTexture(0, tex)", hr);
        hr = device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        PrintHr("E: SetTextureStageState(COLOROP=MODULATE)", hr);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255,0,0,0), 1.0f, 0);
        Dx8TLVertex texQuad[4] = {
            {0.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,255,255), 0, 0.0f, 0.0f},
            {64.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,255,255), 0, 1.0f, 0.0f},
            {64.0f, 64.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,255,255), 0, 1.0f, 1.0f},
            {0.0f, 64.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,255,255), 0, 0.0f, 1.0f},
        };
        hr = device->BeginScene();
        hr = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, quadIdx, D3DFMT_INDEX16, texQuad, sizeof(Dx8TLVertex));
        PrintHr("E: DrawIndexedPrimitiveUP(textured quad)", hr);
        hr = device->EndScene();
        SamplePixel(device, 64, 64, 32, 32, "E-center (expect solid RED (255,0,0) if texture sampled)");
        device->SetTexture(0, nullptr);
        tex->Release();
    }

    // --- Test F: real stencil write+test against the D3DFMT_D24S8 depth-stencil surface already
    // attached via D3DPRESENT_PARAMETERS.EnableAutoDepthStencil above.
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_STENCIL, D3DCOLOR_ARGB(255,0,0,0), 1.0f, 0);
    PrintHr("F: Clear(stencil=0)", hr);
    device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    device->SetRenderState(D3DRS_STENCILREF, 1);
    device->SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
    device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
    device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    device->SetTexture(0, nullptr);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    Dx8TLVertex leftQuad[4] = {
        {0.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,0,255,0), 0, 0, 0},
        {32.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,0,255,0), 0, 0, 0},
        {32.0f, 64.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,0,255,0), 0, 0, 0},
        {0.0f, 64.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,0,255,0), 0, 0, 0},
    };
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, quadIdx, D3DFMT_INDEX16, leftQuad, sizeof(Dx8TLVertex));
    PrintHr("F: DrawIndexedPrimitiveUP(stencil-write pass, left half, green)", hr);
    hr = device->EndScene();
    SamplePixel(device, 64, 64, 16, 32, "F-left-after-write (expect green, stencil now 1 there)");
    SamplePixel(device, 64, 64, 48, 32, "F-right-after-write (expect black, stencil still 0 there)");

    device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    Dx8TLVertex fullQuad[4] = {
        {0.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
        {64.0f, 0.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
        {64.0f, 64.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
        {0.0f, 64.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,255,0,0), 0, 0, 0},
    };
    hr = device->BeginScene();
    hr = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, quadIdx, D3DFMT_INDEX16, fullQuad, sizeof(Dx8TLVertex));
    PrintHr("F: DrawIndexedPrimitiveUP(stencil-test pass, full screen, red)", hr);
    hr = device->EndScene();
    SamplePixel(device, 64, 64, 16, 32, "F-left-after-test (expect RED -- stencil==1, test passed)");
    SamplePixel(device, 64, 64, 48, 32, "F-right-after-test (expect BLACK -- stencil==0, test rejected)");

    // --- Test G: Present() with a real window, just confirming no error.
    hr = device->Present(nullptr, nullptr, nullptr, nullptr);
    PrintHr("G: Present", hr);

    printf("DX8-SPIKE1-DONE\n");
    return 0;
}
