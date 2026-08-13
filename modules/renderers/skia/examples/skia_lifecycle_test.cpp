// SPDX-License-Identifier: MS-PL
// SKIA-12: transactional construction, window registry ownership, and repeated cleanup.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Internal::Renderers::Skia::SkiaInitializationFailurePointEXT;
using CNA::Examples::SdlTestRendererArgs;
using CNA::Examples::SdlTestSurfacePresenter;

namespace
{
    struct FailureCase
    {
        SkiaInitializationFailurePointEXT point;
        const char* stage;
    };

    constexpr std::array<FailureCase, 3> kFailureCases{{
        {SkiaInitializationFailurePointEXT::AfterRenderer, "renderer creation"},
        {SkiaInitializationFailurePointEXT::AfterBackbuffer, "backbuffer creation"},
        {SkiaInitializationFailurePointEXT::AfterRegistration, "renderer registration"},
    }};

    int failures = 0;

    void Check(bool pass, const std::string& label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass)
            ++failures;
    }

    [[nodiscard]] bool ConstructUseAndDestroy(SDL_Window* window)
    {
        bool usable = false;
        {
            SdlTestSurfacePresenter presenter(window);
            SkiaRenderer renderer(SdlTestRendererArgs(
                window, nullptr, &presenter, 16, 12,
                CnaPresentationMode::NativeBackBuffer, 0));
            std::array<std::uint8_t, 4> pixel{};
            renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
            renderer.ReadBackbuffer(0, 0, 1, 1, pixel.data());
            renderer.Present();
            usable = IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == &renderer
                && SDL_GetRenderer(window) != nullptr
                && pixel == std::array<std::uint8_t, 4>{255, 0, 0, 255};
        }
        return usable && IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr
            && SDL_GetRenderer(window) == nullptr && SDL_GetWindowID(window) != 0;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA Skia lifecycle", 64, 48, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool nullRejected = false;
    std::string nullDiagnostic;
    try
    {
        SkiaRenderer invalid({});
    }
    catch (const std::exception& exception)
    {
        nullRejected = true;
        nullDiagnostic = exception.what();
    }
    Check(nullRejected && nullDiagnostic.find("without a platform window") != std::string::npos,
          "missing-platform-window construction fails with an actionable diagnostic");

    for (const FailureCase& failure : kFailureCases)
    {
        bool threw = false;
        std::string diagnostic;
        try
        {
            SdlTestSurfacePresenter presenter(window);
            SkiaRenderer renderer(SdlTestRendererArgs(
                window, nullptr, &presenter, 16, 12,
                CnaPresentationMode::NativeBackBuffer, 0), failure.point);
        }
        catch (const std::exception& exception)
        {
            threw = true;
            diagnostic = exception.what();
        }

        const std::string prefix = std::string("injected failure after ") + failure.stage;
        const std::string expectedDiagnostic =
            std::string("Skia injected initialization failure after ") + failure.stage;
        Check(threw && diagnostic.find(expectedDiagnostic) != std::string::npos,
              prefix + " retains its exact stage diagnostic");
        Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr && SDL_GetRenderer(window) == nullptr
                  && SDL_GetWindowID(window) != 0,
              prefix + " releases renderer/texture/registry and preserves the caller window");
        Check(ConstructUseAndDestroy(window),
              prefix + " permits an immediately usable succeeding renderer");
    }

    bool repeatedLifecycle = true;
    for (int cycle = 0; cycle < 16; ++cycle)
        repeatedLifecycle = repeatedLifecycle && ConstructUseAndDestroy(window);
    Check(repeatedLifecycle, "16 construct/present/destroy cycles leave no renderer or registry state");

    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
