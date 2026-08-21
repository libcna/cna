// DX10-0a: existence-gate spike -- does D3D10CreateDeviceAndSwapChain work at all through Wine's
// own d3d10.dll (a thin wrapper -- Wine's own, NOT overridden; the currently-installed DXVK 2.6.0
// ships no d3d10.dll.so at all, only d3d10core.dll.so) forwarding to DXVK's d3d10core.dll (native
// override) + DXVK's dxgi.dll (native override, inherited from the D3D9/D3D11/D3D12 prefix)?
//
// This is the FIRST open question for a DX10 backend: unlike DX8 (which could avoid the dxgi
// override entirely, since D3D8/D3D9 don't use DXGI swap chains), D3D10 genuinely needs a real
// DXGI factory/swapchain, so this spike deliberately uses dxgi=native (DXVK) -- the SDL3-dxgi-probe
// crash bug (see dx8-spike/README.md) only matters for SDL-based binaries; this raw Win32 spike has
// no SDL at all, so it's unaffected and can answer the device-creation question cleanly first.
#include <windows.h>
#include <d3d10.h>
#include <d3d10misc.h>
#include <dxgi.h>
#include <cstdio>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void PrintHr(const char* what, HRESULT hr) {
    printf("%s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    fflush(stdout);
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx10Spike1Window";
    RegisterClassW(&wc);
    printf("about to create window\n"); fflush(stdout);
    HWND hwnd = CreateWindowExW(0, L"Dx10Spike1Window", L"DX10 Spike1",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    printf("window shown, about to CreateDeviceAndSwapChain\n"); fflush(stdout);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 64;
    scd.BufferDesc.Height = 64;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D10Device* device = nullptr;
    IDXGISwapChain* swapchain = nullptr;
    HRESULT hr = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
        D3D10_SDK_VERSION, &scd, &swapchain, &device);
    PrintHr("D3D10CreateDeviceAndSwapChain", hr);
    if (FAILED(hr)) return 1;

    // Test B: real Clear via a render-target view onto the swap chain's own back buffer.
    ID3D10Texture2D* backBuffer = nullptr;
    hr = swapchain->GetBuffer(0, __uuidof(ID3D10Texture2D), reinterpret_cast<void**>(&backBuffer));
    PrintHr("GetBuffer", hr);
    ID3D10RenderTargetView* rtv = nullptr;
    hr = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    PrintHr("CreateRenderTargetView", hr);
    device->OMSetRenderTargets(1, &rtv, nullptr);
    const float clearColor[4] = { 10.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };
    device->ClearRenderTargetView(rtv, clearColor);
    printf("ClearRenderTargetView called (no HRESULT -- void return)\n");

    hr = swapchain->Present(0, 0);
    PrintHr("Present", hr);

    printf("DX10-SPIKE1-DONE\n");
    return 0;
}
