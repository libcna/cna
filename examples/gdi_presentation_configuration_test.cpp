// SPDX-License-Identifier: MS-PL
// GDI-056: environment-driven presentation branches registered as distinct native CTests.

#include "CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::Gdi;

namespace
{
    constexpr int kColorOnColorStretchMode = 3; // Win32 COLORONCOLOR.
    constexpr int kHalftoneStretchMode = 4;     // Win32 HALFTONE.

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool EnvironmentEquals(const char* name, std::string_view expected)
    {
        const char* value = std::getenv(name);
        return value != nullptr && std::string_view(value) == expected;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: gdi_presentation_configuration_test <default|dirty|halftone>\n");
        return 2;
    }

    const std::string_view variant(argv[1]);
    const bool defaultVariant = variant == "default";
    const bool dirtyVariant = variant == "dirty";
    const bool halftoneVariant = variant == "halftone";
    if (!defaultVariant && !dirtyVariant && !halftoneVariant)
    {
        std::fprintf(stderr, "unknown GDI presentation configuration: %s\n", argv[1]);
        return 2;
    }

    const bool expectDirty = dirtyVariant || halftoneVariant;
    const bool expectHalftone = halftoneVariant;
    bool ok = true;
    ok &= Expect(EnvironmentEquals("CNA_GDI_DIRTY_PRESENTATION", expectDirty ? "1" : "0"),
                 "CTest supplies the expected dirty-presentation configuration");
    ok &= Expect(EnvironmentEquals("CNA_GDI_PRESENT_FILTER",
                                   expectHalftone ? "halftone" : "nearest"),
                 "CTest supplies the expected presentation-filter configuration");
    ok &= Expect(EnvironmentEquals("CNA_GDI_DWM_FLUSH", "0"),
                 "deterministic configuration test disables DwmFlush pacing");
    if (!ok)
        return 1;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int clientWidth = halftoneVariant ? 41 : 32;
    const int clientHeight = halftoneVariant ? 29 : 24;
    const int logicalWidth = halftoneVariant ? 17 : clientWidth;
    const int logicalHeight = halftoneVariant ? 11 : clientHeight;
    SDL_Window* window = SDL_CreateWindow(
        "CNA GDI presentation configuration", clientWidth, clientHeight, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        GdiGraphicsBackend backend(
            window, logicalWidth, logicalHeight,
            halftoneVariant ? CnaPresentationMode::Stretch
                            : CnaPresentationMode::NativeBackBuffer);
        backend.Clear(0.1f, 0.2f, 0.3f, 1.0f);
        backend.Present();

        GdiPresentationTelemetry telemetry;
        ok &= Expect(backend.DebugGetLastPresentationTelemetry(telemetry) &&
                         telemetry.result.success,
                     "configured GDI presentation completes successfully");

        if (halftoneVariant)
        {
            ok &= Expect(telemetry.plan.path == GdiBlitPath::Stretch,
                         "scaled halftone configuration selects StretchDIBits");
            ok &= Expect(telemetry.stretchMode == kHalftoneStretchMode,
                         "halftone environment selects the HALFTONE stretch mode");
        }
        else
        {
            ok &= Expect(telemetry.plan.path == GdiBlitPath::NativeFull &&
                             telemetry.stretchMode == kColorOnColorStretchMode,
                         "unscaled configuration selects full SetDIBitsToDevice with default filter");

            backend.Present();
            ok &= Expect(backend.DebugGetLastPresentationTelemetry(telemetry) &&
                             telemetry.result.success &&
                             telemetry.plan.path ==
                                 (dirtyVariant ? GdiBlitPath::None : GdiBlitPath::NativeFull),
                         dirtyVariant
                             ? "dirty configuration skips a synchronized no-damage present"
                             : "default configuration retains full-frame presentation");
        }

        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI presentation configuration test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
