// SPDX-License-Identifier: MS-PL
// plan_dx.md DX-102: one-time, honest diagnostic for D3D12's real (window-attached) swap-chain
// path -- deliberately NOT registered as a CTest (mirrors this project's own cna_diag_software
// precedent: a real, plain executable a developer/script runs by hand, not part of the default
// green suite). DX-100's own raw-API spike found DXGI_SWAP_EFFECT_FLIP_DISCARD crashes inside
// vanilla Wine's own dxgi.dll when handed a D3D12 command queue; this program re-confirms that
// through the real D3D12GraphicsBackend class (not a standalone spike) so the finding is grounded
// in this backend's own actual code path, not just a throwaway reproduction. A crash here is an
// EXPECTED, already-documented outcome on this Wine dev loop, not a bug to chase -- see
// plan_dx.md DX-102's own row for the full real-run record. Real verification is DX-114's job, on
// real Windows hardware.
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

using CNA::Internal::Backends::GraphicsBackendCreateArgs;
using CNA::Internal::Backends::D3D12::D3D12GraphicsBackend;

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }

    SDL_Window* window = SDL_CreateWindow("cna_d3d12_swapchain_diag", 64, 64, 0);
    if (!window)
    {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 2;
    }

    GraphicsBackendCreateArgs args;
    args.window = window;
    args.virtualWidth = 64;
    args.virtualHeight = 64;

    std::printf("Constructing D3D12GraphicsBackend with a real window (real CreateSwapChainForHwnd attempt)...\n");
    std::fflush(stdout);

    D3D12GraphicsBackend backend(args);

    // If we get here without crashing, print the real, honest outcome.
    std::printf("Backend constructed without crashing.\n");
    std::printf("IsSwapChainAvailableEXT() = %s\n", backend.IsSwapChainAvailableEXT() ? "true" : "false");
    std::printf("GetSwapChainEXT() = %p\n", static_cast<void*>(backend.GetSwapChainEXT()));

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
