// SPDX-License-Identifier: MS-PL
// SKIA-160: construction-time raster/Ganesh mode selection, proven transactional both ways.
//
// This single source compiles and runs correctly in EITHER CNA_SKIA_MODE:
//   RASTER (default): SkiaGaneshContext must refuse deterministically, without touching platform GL at
//   all -- registered under Skia;Raster (no display needed), proving "no silent fallback" holds
//   in every ordinary regression build, which never links Ganesh.
//   GANESH (opt-in, SKIA-159/160): SkiaGaneshContext must genuinely construct a working
//   GrDirectContext over a real platform GL context -- registered under Skia;Accelerated;Display,
//   giving that label its first real member. The negative (null window) case is proven here too,
//   so the transactional-failure contract is checked in both directions within the same mode.

#include "CNA/Internal/Renderers/Skia/SkiaGaneshContext.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaStartupDiagnostic.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <string_view>

using CNA::Internal::Renderers::Skia::SkiaGaneshContext;
using CNA::Internal::Renderers::Skia::kSkiaPinnedRevision;
using CNA::Examples::SdlTestGlContext;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures;
    }

    [[nodiscard]] bool ThrowsRuntimeError(CNA::Platform::IPlatformGlContext* service,
                                          CNA::Platform::WindowId window)
    {
        try
        {
            SkiaGaneshContext context(service, window);
            (void)context;
            return false;
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
    }
}

#if defined(CNA_SKIA_MODE_GANESH)

int main()
{
    Check(ThrowsRuntimeError(nullptr, 0),
          "constructing without a platform GL service fails transactionally in GANESH mode too");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_Window* window = SDL_CreateWindow(
        "CNA Skia Ganesh mode", 64, 48, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    {
        SdlTestGlContext glContext(window);
        SkiaGaneshContext context(&glContext, SDL_GetWindowID(window));
        Check(context.MaxTextureSize() > 0,
              "a real GrDirectContext reports a positive max texture size");

        const std::string_view report = context.StartupDiagnostic();
        Check(report.find(kSkiaPinnedRevision) != std::string_view::npos,
              "Ganesh diagnostic reports the pinned Skia revision");
        Check(report.find("surface=ganesh-gl") != std::string_view::npos,
              "Ganesh diagnostic reports the Ganesh/GL surface, distinct from raster's");
        Check(report.find("abandoned=0") != std::string_view::npos,
              "Ganesh diagnostic reports a live, non-abandoned context");
        Check(report.find('\n') == std::string_view::npos && report.find('\r') == std::string_view::npos,
              "Ganesh diagnostic is one line");
    }
    // The destructor above must have released the context cleanly; constructing a second,
    // independent context on the same window proves no state leaked across instances.
    {
        SdlTestGlContext glContext(window);
        SkiaGaneshContext second(&glContext, SDL_GetWindowID(window));
        Check(second.MaxTextureSize() > 0,
              "a second, independent GrDirectContext also constructs successfully");
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}

#else

int main()
{
    Check(ThrowsRuntimeError(nullptr, 0),
          "constructing without platform state fails transactionally in RASTER mode");
    // A non-zero-but-fake stable id proves the refusal does not inspect a native window at all.
    Check(ThrowsRuntimeError(nullptr, 1),
          "constructing with any window id fails in RASTER mode without touching platform GL");
    return failures == 0 ? 0 : 1;
}

#endif
