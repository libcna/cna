// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32_native_validation.md WINNATIVE-0025: does repeatedly creating and destroying a
// Direct3D 11 device on a CNA window cost anything?
//
// The question is not academic. The native CnaTests run reached a point where
// `D3D11CreateDevice` began returning DXGI_ERROR_UNSUPPORTED (0x887A0004) after thousands of tests
// had each made a device -- while a freshly launched probe on the same machine, at the same
// moment, made one without trouble. Something accumulates across a long-lived process. Two very
// different explanations fit that, and they call for opposite responses:
//
//   * CNA (or this harness) leaks a device, context or swap chain per cycle -- a defect to fix;
//   * the VirtualBox virtual adapter simply will not hand out more than N devices to one process,
//     however diligently they are released -- an environment limitation to record and not to
//     "fix" in CNA.
//
// Telling them apart needs exactly this: a tight loop that releases everything it creates, run
// until either it stops working or the budget runs out, with the process's own object counts
// sampled along the way. If the counts are flat and creation still fails, the driver is the limit.
// If the counts climb, we are the limit.
//
// --warp runs the same loop on the WARP software rasteriser instead of the adapter. That is the
// discriminator that turns an inference into a measurement: WARP is Microsoft's own code and is
// the same on every machine, so if the hardware path leaks and WARP does not, the leak belongs to
// the graphics driver rather than to the D3D11 runtime -- and on this VM the "graphics driver" is
// VirtualBox's.
//
// --hold answers a different question from the rest of this program. The cycling loop creates and
// destroys one device at a time, and 400 of those succeed on this machine, so a sequential budget
// is not what CnaTests runs out of. --hold never releases anything, which is what a process that
// leaks GraphicsDevice objects actually does, and reports the cycle at which creation starts
// failing. The two numbers together say whether a limit is on devices alive at once or on devices
// ever created.
//
// --flip and --renderer-teardown make the loop model what DirectX11Renderer actually does, which the
// defaults do not. By default this builds a DXGI_SWAP_EFFECT_DISCARD, BufferCount 1 swap chain --
// the BLT model -- and releases through ClearState()/Flush(); the renderer uses FLIP_DISCARD with
// two buffers and lets its ComPtr members unwind with neither call. A control that cannot exercise
// the path under suspicion cannot fail, which is how "400 clean cycles" once stood as evidence
// about a renderer this loop never resembled. The defaults are kept so earlier numbers stay
// comparable; the two flags together are the renderer-shaped run.
//
// Usage: cna_win32_d3d11_cycle.exe [--cycles N] [--warp] [--hold] [--flip] [--renderer-teardown]
//                                  [--quiet]
// Exit:  0 every cycle succeeded; 1 creation began failing while our own counts stayed flat
//        (environment limit -- reported, not a defect); 2 the process's counts grew (a leak --
//        --warp says whether it is the driver's); 3 no D3D11 at all on this machine.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <dxgi.h>

#undef CreateWindow
#undef CreateDirectory
#undef MessageBox
#undef GetClassName

namespace
{
    using namespace CNA::Platform;

    constexpr UINT kWidth = 320;
    constexpr UINT kHeight = 240;

    struct Counts
    {
        DWORD user = 0;
        DWORD gdi = 0;
        DWORD handles = 0;
        SIZE_T privateBytes = 0;
    };

    Counts Sample()
    {
        Counts c{};
        const HANDLE self = GetCurrentProcess();
        c.user = GetGuiResources(self, GR_USEROBJECTS);
        c.gdi = GetGuiResources(self, GR_GDIOBJECTS);
        DWORD h = 0;
        if (GetProcessHandleCount(self, &h)) c.handles = h;
        PROCESS_MEMORY_COUNTERS m{};
        m.cb = sizeof(m);
        if (GetProcessMemoryInfo(self, &m, sizeof(m))) c.privateBytes = m.PagefileUsage;
        return c;
    }

    // The counts are the whole process's, and the adapter's user-mode driver lives in this process,
    // so growth alone cannot say whose leak it is. 2026-09-16 on the VirtualBox guest: 1500 cycles
    // flat on WARP, ~6 handles and ~200 KB per cycle on the adapter.
    void PrintLeakOwner(bool warp)
    {
        if (warp)
        {
            std::printf("WARP is Microsoft's own rasteriser, so this loop or the D3D11 runtime "
                        "leaks -- not a graphics driver.\n");
        }
        else
        {
            std::printf("The counts include the adapter's user-mode driver. Run the same flags with "
                        "--warp: if WARP stays flat, the graphics driver leaks, not this loop.\n");
        }
    }
}

int main(int argc, char** argv)
{
    int cycles = 400;
    bool quiet = false;
    bool warp = false;
    bool noD3d = false;
    bool hold = false;
    bool flip = false;
    bool rendererTeardown = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) cycles = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--warp") == 0) warp = true;
        else if (std::strcmp(argv[i], "--no-d3d") == 0) noD3d = true;
        else if (std::strcmp(argv[i], "--hold") == 0) hold = true;
        else if (std::strcmp(argv[i], "--flip") == 0) flip = true;
        else if (std::strcmp(argv[i], "--renderer-teardown") == 0) rendererTeardown = true;
        else if (std::strcmp(argv[i], "--quiet") == 0) quiet = true;
    }
    const D3D_DRIVER_TYPE driverType = warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    // --no-d3d is the control: the identical loop, windows and all, with no device created. If the
    // counts climb here too, the device was never the subject.
    std::printf("driver: %s\n", noD3d ? "NONE (control: windows only, no D3D)"
                                       : (warp ? "WARP (software rasteriser)"
                                               : "HARDWARE (this adapter)"));
    std::printf("swap chain: %s, teardown: %s\n",
                flip ? "FLIP_DISCARD x2 (renderer model)" : "DISCARD x1 (BLT model)",
                rendererTeardown ? "renderer-shaped (no ClearState/Flush)" : "ClearState + Flush");

    std::unique_ptr<IPlatform> platform;
    try { platform = PlatformFactory::Create("Win32"); }
    catch (const std::exception& error)
    {
        std::printf("the Win32 platform could not be created: %s\n", error.what());
        return 3;
    }
    platform->AcquireSubsystem(PlatformSubsystem::Video);
    std::vector<PlatformEvent> events;

    // Held alive deliberately when --hold: this is the leak being simulated, not an oversight.
    std::vector<ID3D11Device*> heldDevices;
    std::vector<ID3D11DeviceContext*> heldContexts;
    std::vector<IDXGISwapChain*> heldSwapChains;
    std::vector<std::unique_ptr<IPlatformWindow>> heldWindows;

    Counts baseline{};
    int firstFailure = -1;
    HRESULT firstFailureHr = S_OK;
    Counts atFailure{};

    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        WindowDescription description;
        description.title = "d3d11 cycle";
        description.width = static_cast<int>(kWidth);
        description.height = static_cast<int>(kHeight);
        description.visible = false;
        std::unique_ptr<IPlatformWindow> window = platform->CreateWindow(description);
        events.clear();
        platform->PollEvents(events);

        Win32NativeWindow native;
        if (!TryGetWin32(window->GetNativeHandle(), native)) { std::printf("no HWND\n"); return 3; }

        if (noD3d)
        {
            window.reset();
            events.clear();
            platform->PollEvents(events);
            if (cycle == 0) baseline = Sample();
            if (!quiet && (cycle % 50 == 0 || cycle == cycles - 1))
            {
                const Counts now = Sample();
                std::printf("  cycle %5d  USER %4lu  GDI %4lu  handles %5lu  private %6llu KB\n",
                            cycle, static_cast<unsigned long>(now.user),
                            static_cast<unsigned long>(now.gdi),
                            static_cast<unsigned long>(now.handles),
                            static_cast<unsigned long long>(now.privateBytes / 1024));
                std::fflush(stdout);
            }
            continue;
        }

        DXGI_SWAP_CHAIN_DESC swapDescription{};
        swapDescription.BufferCount = flip ? 2 : 1;
        swapDescription.BufferDesc.Width = kWidth;
        swapDescription.BufferDesc.Height = kHeight;
        swapDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDescription.OutputWindow = static_cast<HWND>(native.hwnd);
        swapDescription.SampleDesc.Count = 1;
        swapDescription.Windowed = TRUE;
        swapDescription.SwapEffect = flip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* swapChain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        D3D_FEATURE_LEVEL level{};
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, driverType, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &swapDescription, &swapChain, &device, &level, &context);

        if (FAILED(hr) || device == nullptr)
        {
            if (cycle == 0) { std::printf("no Direct3D 11 device at all (hr=0x%08lX)\n",
                                          static_cast<unsigned long>(hr)); return 3; }
            firstFailure = cycle;
            firstFailureHr = hr;
            atFailure = Sample();
            if (swapChain != nullptr) swapChain->Release();
            if (context != nullptr) context->Release();
            if (device != nullptr) device->Release();
            break;
        }

        // Use it, so the cycle is a real one rather than a creation benchmark.
        ID3D11Texture2D* backBuffer = nullptr;
        ID3D11RenderTargetView* view = nullptr;
        if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                           reinterpret_cast<void**>(&backBuffer))) &&
            backBuffer != nullptr)
        {
            if (SUCCEEDED(device->CreateRenderTargetView(backBuffer, nullptr, &view)) &&
                view != nullptr)
            {
                const FLOAT colour[4] = {0.1f, 0.2f, 0.4f, 1.0f};
                context->ClearRenderTargetView(view, colour);
                context->OMSetRenderTargets(1, &view, nullptr);
            }
        }
        swapChain->Present(0, 0);

        if (hold)
        {
            // Keep the device, its context, its swap chain and its window alive, exactly as a
            // process that never releases a GraphicsDevice would.
            if (view != nullptr) view->Release();
            if (backBuffer != nullptr) backBuffer->Release();
            heldContexts.push_back(context);
            heldSwapChains.push_back(swapChain);
            heldDevices.push_back(device);
            heldWindows.push_back(std::move(window));
        }
        else
        {
            // Everything released, in the order a renderer would: views and buffers first, then the
            // swap chain, then the context, then the device. A leak of any one of them is what this
            // program exists to notice.
            if (view != nullptr) view->Release();
            if (backBuffer != nullptr) backBuffer->Release();
            if (rendererTeardown)
            {
                // ~DirectX11Renderer(): no ClearState(), no Flush(); members unwind in reverse
                // declaration order -- views, then the swap chain, then the context, then the
                // device -- with the context still holding its bindings when the chain goes.
                if (swapChain != nullptr) swapChain->Release();
                if (context != nullptr) context->Release();
            }
            else
            {
                if (context != nullptr) { context->ClearState(); context->Flush(); context->Release(); }
                if (swapChain != nullptr) swapChain->Release();
            }
            if (device != nullptr) device->Release();

            window.reset();
        }
        events.clear();
        platform->PollEvents(events);

        // The baseline is taken after the first cycle, not before: the first device loads the
        // driver and its caches, and that cost never comes back.
        if (cycle == 0) baseline = Sample();
        if (!quiet && (cycle % 50 == 0 || cycle == cycles - 1))
        {
            const Counts now = Sample();
            std::printf("  cycle %5d  USER %4lu  GDI %4lu  handles %5lu  private %6llu KB\n",
                        cycle, static_cast<unsigned long>(now.user),
                        static_cast<unsigned long>(now.gdi),
                        static_cast<unsigned long>(now.handles),
                        static_cast<unsigned long long>(now.privateBytes / 1024));
            std::fflush(stdout);
        }
    }

    // Settle before judging. Windows reclaims some kernel objects lazily, and a count sampled the
    // instant the last Release returned can look like a leak that a second later is not one. Pump
    // and wait, then sample again, and report both numbers so the difference is visible rather
    // than hidden inside a verdict.
    const Counts beforeSettle = firstFailure >= 0 ? atFailure : Sample();
    for (int i = 0; i < 40; ++i) { events.clear(); platform->PollEvents(events); ::Sleep(50); }
    const Counts end = firstFailure >= 0 ? atFailure : Sample();
    std::printf("\nbefore settling        : handles %lu  private %llu KB\n",
                static_cast<unsigned long>(beforeSettle.handles),
                static_cast<unsigned long long>(beforeSettle.privateBytes / 1024));
    const bool userGrew = end.user > baseline.user + 8;
    const bool gdiGrew = end.gdi > baseline.gdi + 12;
    const bool handlesGrew = end.handles > baseline.handles + 32;

    std::printf("\nbaseline after cycle 0 : USER %lu  GDI %lu  handles %lu  private %llu KB\n",
                static_cast<unsigned long>(baseline.user), static_cast<unsigned long>(baseline.gdi),
                static_cast<unsigned long>(baseline.handles),
                static_cast<unsigned long long>(baseline.privateBytes / 1024));
    std::printf("end                    : USER %lu  GDI %lu  handles %lu  private %llu KB\n",
                static_cast<unsigned long>(end.user), static_cast<unsigned long>(end.gdi),
                static_cast<unsigned long>(end.handles),
                static_cast<unsigned long long>(end.privateBytes / 1024));

    // --hold is not a leak test -- it IS the leak -- so it answers only the question it was asked:
    // how many devices can be alive at once before the next one is refused.
    if (hold)
    {
        if (firstFailure >= 0)
        {
            std::printf("\nRESULT (--hold): creation failed with hr=0x%08lX after %d devices were "
                        "held alive at once. A process that never releases a device gets this many "
                        "and no more.\n",
                        static_cast<unsigned long>(firstFailureHr), firstFailure);
        }
        else
        {
            std::printf("\nRESULT (--hold): %d devices were held alive at once without a single "
                        "refusal, so the limit is higher than this run asked for.\n", cycles);
        }
        for (auto* c : heldContexts) { if (c != nullptr) c->Release(); }
        for (auto* sc : heldSwapChains) { if (sc != nullptr) sc->Release(); }
        for (auto* d : heldDevices) { if (d != nullptr) d->Release(); }
        heldWindows.clear();
        return firstFailure >= 0 ? 1 : 0;
    }

    if (firstFailure >= 0)
    {
        std::printf("\nD3D11CreateDeviceAndSwapChain began failing at cycle %d with hr=0x%08lX.\n",
                    firstFailure, static_cast<unsigned long>(firstFailureHr));
        if (userGrew || handlesGrew || gdiGrew)
        {
            std::printf("RESULT: the process's object counts grew as well -- a LEAK, not a device "
                        "limit.\n");
            PrintLeakOwner(warp);
            return 2;
        }
        std::printf("RESULT: every cycle released what it created and our object counts stayed "
                    "flat, so the adapter itself stops handing out devices. On this VM that is a "
                    "VIRTUAL GPU LIMITATION, not a CNA defect.\n");
        return 1;
    }

    if (userGrew || handlesGrew || gdiGrew)
    {
        std::printf("\nRESULT: %d cycles all succeeded, but the process's object counts grew -- a "
                    "LEAK.\n", cycles);
        PrintLeakOwner(warp);
        return 2;
    }
    std::printf("\nRESULT: %d create/use/destroy cycles, counts flat, no failure.\n", cycles);
    return 0;
}
