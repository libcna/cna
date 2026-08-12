// SPDX-License-Identifier: MS-PL
// SKIA-18: owner-thread, active-surface, presenter, and destruction-order failure boundaries.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <thread>

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::ISpriteBatchRenderer;
using CNA::Internal::Renderers::Skia::SkiaRenderer;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures;
    }

    [[nodiscard]] bool Contains(const std::string& value, const char* fragment)
    {
        return value.find(fragment) != std::string::npos;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA Skia ownership", 64, 48, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::unique_ptr<ISpriteBatchRenderer> lateBatch;
    std::unique_ptr<IRenderTargetRenderer> lateTarget;
    {
        SkiaRenderer renderer(window, 16, 12, CnaPresentationMode::NativeBackBuffer, 0);
        Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == &renderer
                  && SDL_GetRenderer(window) != nullptr,
              "renderer owns the registered SDL presenter on its construction thread");

        lateBatch = renderer.CreateSpriteBatch();
        std::string rendererDiagnostic;
        std::string batchDiagnostic;
        std::thread foreignThread([&]
        {
            try
            {
                renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
            }
            catch (const std::exception& exception)
            {
                rendererDiagnostic = exception.what();
            }

            try
            {
                lateBatch->Begin();
            }
            catch (const std::exception& exception)
            {
                batchDiagnostic = exception.what();
            }
        });
        foreignThread.join();

        Check(Contains(rendererDiagnostic, "owner thread") && Contains(rendererDiagnostic, "Clear"),
              "foreign-thread renderer work fails before touching the active SkSurface");
        Check(Contains(batchDiagnostic, "owner thread") && Contains(batchDiagnostic, "SpriteBatch::Begin"),
              "foreign-thread SpriteBatch work fails before changing its Begin state");

        bool ownerBatchUsable = true;
        try
        {
            lateBatch->Begin();
            lateBatch->End();
        }
        catch (const std::exception&)
        {
            ownerBatchUsable = false;
        }
        Check(ownerBatchUsable, "owner-thread SpriteBatch remains usable after the rejected call");

        lateTarget = renderer.CreateRenderTarget2D(4, 4, 0, true, false, 0);
        renderer.SetRenderTarget2D(lateTarget.get());
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        std::array<std::uint8_t, 4> pixel{};
        renderer.ReadBackbuffer(2, 2, 1, 1, pixel.data());
        Check(pixel == std::array<std::uint8_t, 4>{0, 255, 0, 255},
              "validated active target owns the selected raster surface");

        // Deliberately leave the target selected. Renderer destruction must invalidate only the
        // weak binding; neither the late SpriteBatch nor late target may retain a raw live route.
    }

    Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr && SDL_GetRenderer(window) == nullptr,
          "renderer destruction releases presenter/registry with a late active target");

    std::string afterRendererDiagnostic;
    try
    {
        lateBatch->Begin();
    }
    catch (const std::exception& exception)
    {
        afterRendererDiagnostic = exception.what();
    }
    Check(Contains(afterRendererDiagnostic, "after graphics renderer destruction")
              && Contains(afterRendererDiagnostic, "SpriteBatch::Begin"),
          "late SpriteBatch fails safely before dereferencing destroyed renderer state");
    lateBatch.reset();

    lateTarget.reset();
    Check(SDL_GetWindowID(window) != 0 && SDL_GetRenderer(window) == nullptr,
          "late target destruction is a noexcept weak-binding cleanup and preserves the caller window");

    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("=== %d/%d PASS ===\n", 8 - failures, 8);
    return failures == 0 ? 0 : 1;
}
