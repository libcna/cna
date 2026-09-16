// SPDX-License-Identifier: MS-PL
//
// A Direct3D 11 frame-pacing probe with no CNA in it at all.
//
// cna_demo_2d on the win10_local VirtualBox guest draws at 10-15 fps with vsync on and ~270 fps
// with it off, while the frame itself costs ~0.07 ms. That points at the guest's vblank -- but only
// indirectly, and "it's the VM" is exactly the kind of claim that has to be measured rather than
// asserted. This is the discriminator: the smallest honest D3D11 application (one window, one
// swap chain, clear, Present) timed the same way. If it is also slow, the pacing belongs to the
// environment; if it runs at the display rate, CNA is doing something wrong.
//
// Each configuration presents a fixed number of frames and reports the interval between
// consecutive Present calls:
//
//   flip-vsync     DXGI_SWAP_EFFECT_FLIP_DISCARD, BufferCount 2, Present(1, 0)  -- what CNA does
//   flip-novsync   the same, Present(0, 0)
//   blt-vsync      DXGI_SWAP_EFFECT_DISCARD, BufferCount 1, Present(1, 0)       -- the legacy model
//
// Usage: vsync_probe.exe [frames]      (default 180)
// Build: cl /nologo /EHsc /O2 vsync_probe.cpp /link d3d11.lib dxgi.lib user32.lib

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    bool RunConfiguration(const char* name, HWND hwnd, const bool flip, const UINT syncInterval,
                          const int frames)
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 800;
        desc.BufferDesc.Height = 480;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = flip ? 2 : 1;
        desc.OutputWindow = hwnd;
        desc.Windowed = TRUE;
        desc.SwapEffect = flip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGISwapChain* swapChain = nullptr;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
            &swapChain, &device, nullptr, &context);
        if (FAILED(hr))
        {
            std::printf("%-14s D3D11CreateDeviceAndSwapChain failed hr=0x%08lX\n", name,
                        static_cast<unsigned long>(hr));
            return false;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
        ID3D11RenderTargetView* rtv = nullptr;
        device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
        backBuffer->Release();

        using Clock = std::chrono::steady_clock;
        std::vector<double> intervals;
        intervals.reserve(static_cast<std::size_t>(frames));
        Clock::time_point last{};
        const Clock::time_point start = Clock::now();
        for (int frame = 0; frame < frames; ++frame)
        {
            Pump();
            const float shade = static_cast<float>(frame % 60) / 60.0f;
            const float colour[4] = {shade, 0.2f, 1.0f - shade, 1.0f};
            context->OMSetRenderTargets(1, &rtv, nullptr);
            context->ClearRenderTargetView(rtv, colour);
            swapChain->Present(syncInterval, 0);
            const Clock::time_point now = Clock::now();
            if (frame > 0)
                intervals.push_back(std::chrono::duration<double, std::milli>(now - last).count());
            last = now;
        }
        const double wall = std::chrono::duration<double>(Clock::now() - start).count();

        std::sort(intervals.begin(), intervals.end());
        double sum = 0.0;
        for (double value : intervals) sum += value;
        const auto pct = [&](double p) { return intervals[static_cast<std::size_t>(p * (intervals.size() - 1))]; };
        std::printf("%-14s frames=%d wall_s=%.2f fps=%.1f interval_ms mean=%.2f p50=%.2f p95=%.2f max=%.2f\n",
                    name, frames, wall, frames / wall, sum / intervals.size(), pct(0.5), pct(0.95),
                    intervals.back());

        rtv->Release();
        swapChain->Release();
        context->Release();
        device->Release();
        return true;
    }
}

int main(int argc, char** argv)
{
    const int frames = argc > 1 ? std::max(10, std::atoi(argv[1])) : 180;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CnaVsyncProbe";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW); // IDC_ARROW is a narrow resource id without UNICODE
    RegisterClassExW(&wc);

    RECT rect{0, 0, 800, 480};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    int ok = 0;
    for (const auto& config : {std::make_tuple("flip-vsync", true, 1u), std::make_tuple("flip-novsync", true, 0u),
                               std::make_tuple("blt-vsync", false, 1u)})
    {
        // A fresh window per configuration: a window that has once had a flip-model swap chain
        // cannot take a BLT-model one.
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"D3D11 vsync probe", WS_OVERLAPPEDWINDOW,
                                    100, 100, rect.right - rect.left, rect.bottom - rect.top,
                                    nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(hwnd, SW_SHOW);
        Pump();
        ok += RunConfiguration(std::get<0>(config), hwnd, std::get<1>(config), std::get<2>(config), frames) ? 1 : 0;
        DestroyWindow(hwnd);
        Pump();
    }
    std::fflush(stdout);
    return ok == 3 ? 0 : 1;
}
