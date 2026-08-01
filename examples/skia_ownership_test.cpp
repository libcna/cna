// SPDX-License-Identifier: MS-PL
// SKIA-18: owner-thread, active-surface, presenter, and destruction-order failure boundaries.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <thread>

using CNA::Internal::Backends::CnaPresentationMode;
using CNA::Internal::Backends::IGraphicsBackend;
using CNA::Internal::Backends::IRenderTargetBackend;
using CNA::Internal::Backends::ISpriteBatchBackend;
using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;

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

    std::unique_ptr<ISpriteBatchBackend> lateBatch;
    std::unique_ptr<IRenderTargetBackend> lateTarget;
    {
        SkiaGraphicsBackend backend(window, 16, 12, CnaPresentationMode::NativeBackBuffer, 0);
        Check(IGraphicsBackend::GetForWindow(window) == &backend
                  && SDL_GetRenderer(window) == backend.GetRendererInternal(),
              "backend owns the registered SDL presenter on its construction thread");

        lateBatch = backend.CreateSpriteBatch();
        std::string backendDiagnostic;
        std::string batchDiagnostic;
        std::thread foreignThread([&]
        {
            try
            {
                backend.Clear(1.0f, 0.0f, 0.0f, 1.0f);
            }
            catch (const std::exception& exception)
            {
                backendDiagnostic = exception.what();
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

        Check(Contains(backendDiagnostic, "owner thread") && Contains(backendDiagnostic, "Clear"),
              "foreign-thread backend work fails before touching the active SkSurface");
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

        lateTarget = backend.CreateRenderTarget2D(4, 4, 0, true, false, 0);
        backend.SetRenderTarget2D(lateTarget.get());
        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        std::array<std::uint8_t, 4> pixel{};
        backend.ReadBackbuffer(2, 2, 1, 1, pixel.data());
        Check(pixel == std::array<std::uint8_t, 4>{0, 255, 0, 255},
              "validated active target owns the selected raster surface");

        // Deliberately leave the target selected. Backend destruction must invalidate only the
        // weak binding; neither the late SpriteBatch nor late target may retain a raw live route.
    }

    Check(IGraphicsBackend::GetForWindow(window) == nullptr && SDL_GetRenderer(window) == nullptr,
          "backend destruction releases presenter/registry with a late active target");

    std::string afterBackendDiagnostic;
    try
    {
        lateBatch->Begin();
    }
    catch (const std::exception& exception)
    {
        afterBackendDiagnostic = exception.what();
    }
    Check(Contains(afterBackendDiagnostic, "after graphics backend destruction")
              && Contains(afterBackendDiagnostic, "SpriteBatch::Begin"),
          "late SpriteBatch fails safely before dereferencing destroyed backend state");
    lateBatch.reset();

    lateTarget.reset();
    Check(SDL_GetWindowID(window) != 0 && SDL_GetRenderer(window) == nullptr,
          "late target destruction is a noexcept weak-binding cleanup and preserves the caller window");

    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("=== %d/%d PASS ===\n", 8 - failures, 8);
    return failures == 0 ? 0 : 1;
}
