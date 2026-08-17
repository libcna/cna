// SPDX-License-Identifier: MS-PL
// Minimal integration test for the Win32 GDI renderer. It verifies that CNA gets an HWND from SDL,
// uses GDI Present() successfully, keeps its 2D framebuffer readable, and rejects 3D access.

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace CNA::Internal::Renderers;

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI smoke", 32, 32, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = SDL_GetWindowID(window);
        args.virtualWidth = 32;
        args.virtualHeight = 32;
        args.presentationMode = CnaPresentationMode::Stretch;
        std::unique_ptr<IGraphicsRenderer> renderer = Gdi::CreateGraphicsRenderer(args);

        if (CNA::getCurrentGraphicsRendererType() != CNA::GraphicsRendererType::Gdi ||
            CNA::getCurrentGraphicsRendererName() != "GDI")
        {
            std::fprintf(stderr, "GDI renderer identity is wrong.\n");
            result = 1;
        }

        renderer->Clear(1.0f, 0.0f, 0.0f, 1.0f);
        std::array<std::uint8_t, 4> pixel{};
        renderer->ReadBackbuffer(0, 0, 1, 1, pixel.data());
        if (pixel != std::array<std::uint8_t, 4>{ 255, 0, 0, 255 })
        {
            std::fprintf(stderr, "GDI CPU backbuffer clear/readback mismatch.\n");
            result = 1;
        }

        renderer->Present();

        bool vertexBufferRejected = false;
        try { (void)renderer->CreateVertexBuffer(3); }
        catch (const System::NotSupportedException&) { vertexBufferRejected = true; }
        if (renderer->SupportsCapability(CNA::GraphicsCapability::ThreeD) || !vertexBufferRejected)
        {
            std::fprintf(stderr, "GDI must expose a 2D-only contract.\n");
            result = 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI smoke test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
