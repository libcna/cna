// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md DX-102/DX-116: one-time, honest diagnostic for D3D12's real (window-attached)
// swap-chain path -- deliberately NOT registered as a CTest (mirrors this project's own
// cna_diag_software precedent: a real, plain executable a developer/script runs by hand, not part
// of the default green suite). Under vanilla Wine's own dxgi.dll (scripts/run-wine-vkd3d.sh),
// DXGI_SWAP_EFFECT_FLIP_DISCARD crashes when handed a D3D12 command queue -- DX-100/DX-102's own
// real finding. Under a properly Proton-managed launch (scripts/run-proton-vkd3d.sh), the swap
// chain genuinely works (DX-102's own later update) -- this diagnostic now also exercises DX-116's
// real Clear()+Present() cycle for several frames, proving the whole pipeline end to end, not just
// swap-chain creation. Real windowed CTest coverage is not attempted here (Proton's own bootstrap
// launch is too heavy/slow for a normal CTest run) -- this manual diagnostic plus DX-114's own real
// Windows hardware pass are the actual verification path for the presentation side of this
// renderer.
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>

using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;

int main()
{
    // File-based logging: this diagnostic is also run via Proton's own launcher (STEAM_COMPAT_*),
    // whose stdout is not reliably captured by a wrapping shell -- a file guarantees the real
    // outcome is observable regardless of what's wrapping this process.
    std::FILE* log = std::fopen("C:\\directx12_swapchain_diag.log", "w");
    if (!log) log = stdout;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(log, "SDL_Init failed: %s\n", SDL_GetError());
        std::fflush(log);
        return 2;
    }

    SDL_Window* window = SDL_CreateWindow("cna_d3d12_swapchain_diag", 64, 64, 0);
    if (!window)
    {
        std::fprintf(log, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::fflush(log);
        return 2;
    }

    GraphicsRendererCreateArgs args;
    args.surface = CNA::Examples::SdlTestSurface(window);
    args.virtualWidth = 64;
    args.virtualHeight = 64;

    std::fprintf(log, "Constructing DirectX12Renderer with a real window (real CreateSwapChainForHwnd attempt)...\n");
    std::fflush(log);

    std::unique_ptr<DirectX12Renderer> renderer;
    try
    {
        renderer = std::make_unique<DirectX12Renderer>(args);
    }
    catch (const std::exception& ex)
    {
        std::fprintf(log, "DirectX12Renderer construction threw: %s\n", ex.what());
        std::fflush(log);
        SDL_DestroyWindow(window);
        SDL_Quit();
        if (log != stdout) std::fclose(log);
        return 3;
    }

    // If we get here without crashing, print the real, honest outcome.
    std::fprintf(log, "Renderer constructed without crashing.\n");
    std::fprintf(log, "IsSwapChainAvailableEXT() = %s\n", renderer->IsSwapChainAvailableEXT() ? "true" : "false");
    std::fprintf(log, "GetSwapChainEXT() = %p\n", static_cast<void*>(renderer->GetSwapChainEXT()));
    std::fflush(log);

    int failures = 0;
    const auto check = [&](const bool condition, const char* message)
    {
        std::fprintf(log, "[%s] %s\n", condition ? "PASS" : "FAIL", message);
        std::fflush(log);
        if (!condition) ++failures;
    };

    // DX-116: real Clear()+Present() cycle, several frames, proving the whole pipeline (back-buffer
    // acquisition, PRESENT<->RENDER_TARGET barrier transitions, real IDXGISwapChain3::Present())
    // works end to end, not just swap-chain creation -- a different color each frame so a human
    // reviewing this log (or, on real Windows, actually watching the window) can tell each frame
    // genuinely reached the screen rather than the same clear silently repeating.
    if (renderer->IsSwapChainAvailableEXT())
    {
        const int frameCount = 10;
        for (int i = 0; i < frameCount; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(frameCount - 1);
            renderer->Clear(t, 1.0f - t, 0.25f, 1.0f);
            renderer->Present();
            std::fprintf(log, "Frame %d: Clear()+Present() returned without throwing (t=%.2f).\n", i, t);
            std::fflush(log);
        }
        std::fprintf(log, "All %d frames presented without throwing or crashing.\n", frameCount);
        std::fflush(log);
    }
    else
    {
        std::fprintf(log, "Swap chain unavailable -- skipping the Clear()+Present() loop.\n");
        std::fflush(log);
    }

    // DX-218: exercise the real ResizeBuffers path rather than merely observing that a logical
    // viewport changed. Alternating two asymmetric sizes catches stale width and height
    // independently; reading every pixel after each clear catches a stretched/cropped old frame.
    if (renderer->IsSwapChainAvailableEXT())
    {
        const auto heaps = renderer->GetDescriptorHeapsEXT();
        const auto rtvBefore = heaps->rtv.GetStatsEXT();
        const auto dsvBefore = heaps->dsv.GetStatsEXT();
        const std::size_t trackedBefore = renderer->GetResourceStateTrackerEXT().GetTrackedCountEXT();

        try
        {
            for (int cycle = 0; cycle < 20; ++cycle)
            {
                const int targetWidth = (cycle % 2 == 0) ? 96 : 72;
                const int targetHeight = (cycle % 2 == 0) ? 80 : 88;
                if (!SDL_SetWindowSize(window, targetWidth, targetHeight))
                {
                    check(false, "SDL_SetWindowSize accepted the requested resize");
                    break;
                }
                (void)SDL_SyncWindow(window);

                bool windowReachedSize = false;
                for (int attempt = 0; attempt < 200; ++attempt)
                {
                    SDL_PumpEvents();
                    int actualWidth = 0;
                    int actualHeight = 0;
                    SDL_GetWindowSizeInPixels(window, &actualWidth, &actualHeight);
                    if (actualWidth == targetWidth && actualHeight == targetHeight)
                    {
                        windowReachedSize = true;
                        break;
                    }
                    SDL_Delay(10);
                }
                if (!windowReachedSize)
                {
                    check(false, "the virtual compositor applied the requested drawable size");
                    break;
                }

                renderer->OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
                renderer->Present();

                const D3D12_RESOURCE_DESC resizedDesc =
                    renderer->GetCurrentBackBufferResourceEXT()->GetDesc();
                const bool dimensionsMatch =
                    resizedDesc.Width == static_cast<UINT64>(targetWidth)
                    && resizedDesc.Height == static_cast<UINT>(targetHeight);
                check(dimensionsMatch, "ResizeBuffers produced the requested back-buffer dimensions");

                const std::uint8_t expected[4] = {
                    static_cast<std::uint8_t>((cycle % 3) == 0 ? 255 : 0),
                    static_cast<std::uint8_t>((cycle % 3) == 1 ? 255 : 0),
                    static_cast<std::uint8_t>((cycle % 3) == 2 ? 255 : 0),
                    255};
                renderer->Clear(
                    static_cast<float>(expected[0]) / 255.0f,
                    static_cast<float>(expected[1]) / 255.0f,
                    static_cast<float>(expected[2]) / 255.0f,
                    1.0f);
                std::vector<std::uint8_t> pixels(
                    static_cast<std::size_t>(targetWidth)
                    * static_cast<std::size_t>(targetHeight) * 4);
                renderer->ReadBackbuffer(
                    0, 0, targetWidth, targetHeight, pixels.data());
                bool everyPixelMatches = true;
                for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
                {
                    if (pixels[offset] != expected[0] || pixels[offset + 1] != expected[1]
                        || pixels[offset + 2] != expected[2] || pixels[offset + 3] != expected[3])
                    {
                        everyPixelMatches = false;
                        break;
                    }
                }
                check(everyPixelMatches,
                      "the newly-sized back buffer contains the new clear colour in every pixel");
                renderer->Present();
            }
        }
        catch (const std::exception& ex)
        {
            std::fprintf(log, "[FAIL] resize cycle threw: %s\n", ex.what());
            std::fflush(log);
            ++failures;
        }

        const auto rtvAfter = heaps->rtv.GetStatsEXT();
        const auto dsvAfter = heaps->dsv.GetStatsEXT();
        check(rtvAfter.live == rtvBefore.live && rtvAfter.capacity == rtvBefore.capacity
                  && rtvAfter.heapObjects == rtvBefore.heapObjects,
              "20 resize cycles leave RTV live count, capacity, and heap count unchanged");
        check(dsvAfter.live == dsvBefore.live && dsvAfter.capacity == dsvBefore.capacity
                  && dsvAfter.heapObjects == dsvBefore.heapObjects,
              "20 resize cycles leave DSV live count, capacity, and heap count unchanged");
        check(rtvAfter.recycles >= rtvBefore.recycles + 40,
              "the two swap-chain RTV slots were recycled on every resize");
        check(dsvAfter.recycles >= dsvBefore.recycles + 20,
              "the default depth-stencil descriptor was recycled on every resize");
        check(renderer->GetResourceStateTrackerEXT().GetTrackedCountEXT() == trackedBefore,
              "resource-state tracking has no stale entry growth across resize cycles");
    }

    std::fprintf(log, "DX-218 result: %s (%d failure%s).\n",
                 failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    std::fflush(log);

    renderer.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (log != stdout) std::fclose(log);
    return failures == 0 ? 0 : 1;
}
