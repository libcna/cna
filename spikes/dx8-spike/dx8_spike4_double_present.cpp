// DX8 follow-up spike: isolate a real AMD RADV Vulkan driver bug found via the full CTest suite --
// IDirect3DDevice8::Present() (via DXVK/D8VK) succeeds once, then the SECOND consecutive
// Clear()+Present() call fails with VK_ERROR_SURFACE_LOST_KHR ("Presenter: Got
// VK_ERROR_SURFACE_LOST_KHR, recreating swapchain" / "Presenter: Failed to get surface
// capabilities"), which Wine's X11 error handler treats as fatal (process aborts, "X connection to
// :99 broken"). Reproduced here with ZERO CNA/SDL code -- just raw Direct3DCreate8/CreateDevice/
// Clear/Present -- proving this is not a defect in Dx8GraphicsBackend.cpp: every DX8 CTest's own
// Draw() calls Present() once explicitly and the CNA framework calls EndDraw()->Present() again
// automatically right after, so this bug was hit by every single test in the suite.
//
// Confirmed fix (see scripts/run-wine-dx8.sh): forcing DXVK onto the llvmpipe (software) Vulkan
// device instead of the real RADV GPU avoids the bug entirely -- run this binary once with the
// default environment (crashes on "Present 2") and once with DXVK_FILTER_DEVICE_NAME=llvmpipe
// (all 3 Clear+Present cycles succeed) to reproduce both sides.
#include <windows.h>
#include <d3d8.h>
#include <cstdio>

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
    wc.lpszClassName = L"Dx8DoublePresentWindow";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx8DoublePresentWindow", L"DX8 Double Present",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    IDirect3D8* d3d8 = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d8) { printf("Direct3DCreate8 failed\n"); return 1; }

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
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    IDirect3DDevice8* device = nullptr;
    HRESULT hr = d3d8->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    PrintHr("CreateDevice", hr);
    if (FAILED(hr)) return 1;

    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 10, 20, 30), 1.0f, 0);
    PrintHr("Clear 1", hr);
    hr = device->Present(nullptr, nullptr, nullptr, nullptr);
    PrintHr("Present 1", hr);

    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 40, 50, 60), 1.0f, 0);
    PrintHr("Clear 2", hr);
    hr = device->Present(nullptr, nullptr, nullptr, nullptr);
    PrintHr("Present 2", hr);

    hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 70, 80, 90), 1.0f, 0);
    PrintHr("Clear 3", hr);
    hr = device->Present(nullptr, nullptr, nullptr, nullptr);
    PrintHr("Present 3", hr);

    printf("DX8-DOUBLE-PRESENT-DONE\n");
    return 0;
}
