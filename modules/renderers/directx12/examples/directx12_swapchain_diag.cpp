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

#include <SDL3/SDL.h>

#include <cstdio>

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
    args.surface.windowId = SDL_GetWindowID(window);
    args.virtualWidth = 64;
    args.virtualHeight = 64;

    std::fprintf(log, "Constructing DirectX12Renderer with a real window (real CreateSwapChainForHwnd attempt)...\n");
    std::fflush(log);

    DirectX12Renderer renderer(args);

    // If we get here without crashing, print the real, honest outcome.
    std::fprintf(log, "Renderer constructed without crashing.\n");
    std::fprintf(log, "IsSwapChainAvailableEXT() = %s\n", renderer.IsSwapChainAvailableEXT() ? "true" : "false");
    std::fprintf(log, "GetSwapChainEXT() = %p\n", static_cast<void*>(renderer.GetSwapChainEXT()));
    std::fflush(log);

    // DX-116: real Clear()+Present() cycle, several frames, proving the whole pipeline (back-buffer
    // acquisition, PRESENT<->RENDER_TARGET barrier transitions, real IDXGISwapChain3::Present())
    // works end to end, not just swap-chain creation -- a different color each frame so a human
    // reviewing this log (or, on real Windows, actually watching the window) can tell each frame
    // genuinely reached the screen rather than the same clear silently repeating.
    if (renderer.IsSwapChainAvailableEXT())
    {
        const int frameCount = 10;
        for (int i = 0; i < frameCount; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(frameCount - 1);
            renderer.Clear(t, 1.0f - t, 0.25f, 1.0f);
            renderer.Present();
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

    SDL_DestroyWindow(window);
    SDL_Quit();
    if (log != stdout) std::fclose(log);
    return 0;
}
