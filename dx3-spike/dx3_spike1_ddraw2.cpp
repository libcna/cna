// DX30-0 existence-gate spike: confirm IDirectDraw2 is genuinely reachable and functional in this
// environment's Wine (10.0~repack-6), not just present in the MinGW-w64 headers. Mirrors DX1-0's/
// DX2-0's discipline of spiking a new COM interface revision before writing any backend code.
//
// Test A: dd->QueryInterface(IID_IDirectDraw2, ...) succeeds on a real DirectDrawCreate()'d object.
// Test B: IDirectDraw2::GetAvailableVidMem (a genuinely v2-exclusive method -- does not exist on
//         IDirectDraw v1 at all) returns a real HRESULT and plausible non-zero memory totals, not
//         just a stub/E_NOTIMPL.
// Test C: CreateSurface (a method IDirectDraw2 also exposes, inherited/duplicated from v1) still
//         works correctly when called through the v2 interface pointer instead of the v1 one --
//         proves the v2 object is a fully-functional drop-in, not merely a QueryInterface-only
//         formality.
// Test D: SetDisplayMode's 5-argument v2 signature (adds dwRefreshRate) is callable without error
//         in windowed DDSCL_NORMAL mode (DX1-0/DX2-0 already found windowed mode never NEEDS this
//         call -- this just confirms the wider signature itself doesn't fail if exercised).
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
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
    wc.lpszClassName = L"Dx3Spike1Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx3Spike1Window", L"DX3 Spike1",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    LPDIRECTDRAW dd = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &dd, nullptr);
    PrintHr("DirectDrawCreate (v1)", hr);
    hr = dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    PrintHr("SetCooperativeLevel", hr);

    // --- Test A: QueryInterface(IID_IDirectDraw2)
    LPDIRECTDRAW2 dd2 = nullptr;
    hr = dd->QueryInterface(IID_IDirectDraw2, (void**)&dd2);
    PrintHr("QueryInterface(IID_IDirectDraw2)", hr);
    if (FAILED(hr)) { printf("DX30-SPIKE1-ABORT: no IDirectDraw2\n"); return 1; }

    // --- Test B: GetAvailableVidMem (v2-exclusive)
    DDSCAPS caps; memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    DWORD total = 0, free_ = 0;
    hr = dd2->GetAvailableVidMem(&caps, &total, &free_);
    PrintHr("GetAvailableVidMem", hr);
    printf("GetAvailableVidMem: total=%lu free=%lu\n", (unsigned long)total, (unsigned long)free_);

    // --- Test C: CreateSurface via the v2 pointer still works
    DDSURFACEDESC desc; memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = 32; desc.dwHeight = 32;
    LPDIRECTDRAWSURFACE surf = nullptr;
    hr = dd2->CreateSurface(&desc, &surf, nullptr);
    PrintHr("IDirectDraw2::CreateSurface", hr);
    if (SUCCEEDED(hr)) {
        DDSURFACEDESC lockDesc; memset(&lockDesc, 0, sizeof(lockDesc));
        lockDesc.dwSize = sizeof(lockDesc);
        hr = surf->Lock(nullptr, &lockDesc, DDLOCK_WAIT, nullptr);
        PrintHr("  Lock() on v2-created surface", hr);
        if (SUCCEEDED(hr)) surf->Unlock(lockDesc.lpSurface);
        surf->Release();
    }

    // --- Test D: SetDisplayMode's wider v2 signature (5 args) doesn't fail when called (informational
    // only -- DX1-0/DX2-0 already established windowed mode never needs this call at all).
    hr = dd2->SetDisplayMode(640, 480, 32, 60, 0);
    PrintHr("IDirectDraw2::SetDisplayMode(640,480,32,60Hz,0)", hr);
    if (SUCCEEDED(hr)) dd2->RestoreDisplayMode();

    // --- Test E: QueryInterface(IID_IDirect3D2) FROM the v2 pointer (not v1) -- DX3's whole 3D
    // layer depends on this chain still working the same way DX2's did from its v1 pointer.
    LPDIRECT3D2 d3d2 = nullptr;
    hr = dd2->QueryInterface(IID_IDirect3D2, (void**)&d3d2);
    PrintHr("IDirectDraw2::QueryInterface(IID_IDirect3D2)", hr);
    if (SUCCEEDED(hr)) {
        DDSURFACEDESC rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
        rtDesc.dwSize = sizeof(rtDesc);
        rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
        rtDesc.dwWidth = 64; rtDesc.dwHeight = 64;
        LPDIRECTDRAWSURFACE rt = nullptr;
        hr = dd2->CreateSurface(&rtDesc, &rt, nullptr);
        PrintHr("  CreateSurface(3d-device rt via v2)", hr);
        if (SUCCEEDED(hr)) {
            LPDIRECT3DDEVICE2 device = nullptr;
            hr = d3d2->CreateDevice(IID_IDirect3DRGBDevice, rt, &device);
            PrintHr("  IDirect3D2::CreateDevice(RGBDevice2) off a v2-obtained IDirect3D2", hr);
            if (SUCCEEDED(hr)) device->Release();
            rt->Release();
        }
        d3d2->Release();
    }

    printf("DX30-SPIKE1-DONE\n");
    return 0;
}
