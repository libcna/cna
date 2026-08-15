// SPDX-License-Identifier: MS-PL
// GDI-056/GDI-072: registered variants plus immutable typed configuration coverage.

#include "CNA/Internal/Renderers/Gdi/GdiConfiguration.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;

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

    std::size_t CountOccurrences(std::string_view text, std::string_view needle)
    {
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = text.find(needle, position)) != std::string_view::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    class ScopedEnvironmentVariable final
    {
    public:
        explicit ScopedEnvironmentVariable(const char* name) : name_(name)
        {
            const char* value = std::getenv(name_.c_str());
            if (value != nullptr)
            {
                hadOriginalValue_ = true;
                originalValue_ = value;
            }
        }

        ~ScopedEnvironmentVariable()
        {
            (void)_putenv_s(name_.c_str(),
                            hadOriginalValue_ ? originalValue_.c_str() : "");
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
        ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

        bool Set(const char* value) const
        {
            return _putenv_s(name_.c_str(), value) == 0;
        }

    private:
        std::string name_;
        std::string originalValue_;
        bool hadOriginalValue_ = false;
    };
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

    const GdiConfigurationParseResult defaultParse = ParseGdiConfiguration({});
    ok &= Expect(defaultParse.configuration == GdiConfiguration{} &&
                     defaultParse.diagnostic.empty(),
                 "missing GDI settings parse to quiet safe defaults");

    const GdiConfigurationParseResult validParse = ParseGdiConfiguration({
        std::string_view("halftone"), std::string_view("1"), std::string_view("1")});
    ok &= Expect(validParse.configuration ==
                     GdiConfiguration{GdiPresentationFilter::Halftone, true, true} &&
                     validParse.diagnostic.empty(),
                 "all documented GDI setting values parse into typed policy");

    const GdiConfigurationParseResult invalidParse = ParseGdiConfiguration({
        std::string_view("linear\nfilter"), std::string_view("yes"), std::string_view("2")});
    ok &= Expect(invalidParse.configuration == GdiConfiguration{},
                 "invalid GDI values retain per-setting safe defaults");
    ok &= Expect(!invalidParse.diagnostic.empty() &&
                     invalidParse.diagnostic.find_first_of("\r\n") == std::string::npos &&
                     invalidParse.diagnostic.find("linear\\nfilter") != std::string::npos,
                 "invalid values produce one sanitized single-line diagnostic");
    ok &= Expect(CountOccurrences(invalidParse.diagnostic, "CNA_GDI_PRESENT_FILTER") == 1 &&
                     CountOccurrences(invalidParse.diagnostic,
                                      "CNA_GDI_DIRTY_PRESENTATION") == 1 &&
                     CountOccurrences(invalidParse.diagnostic, "CNA_GDI_DWM_FLUSH") == 1,
                 "the aggregate diagnostic names every invalid setting exactly once");

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
        {
            GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
                window, nullptr, nullptr, logicalWidth, logicalHeight,
                halftoneVariant ? CnaPresentationMode::Stretch
                                : CnaPresentationMode::NativeBackBuffer));
            const GdiConfiguration expectedConfiguration{
                expectHalftone ? GdiPresentationFilter::Halftone
                               : GdiPresentationFilter::Nearest,
                expectDirty,
                false};
            ok &= Expect(renderer.DebugGetConfiguration() == expectedConfiguration,
                         "renderer captures the registered environment as typed construction state");

            renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
            renderer.Present();

            GdiPresentationTelemetry telemetry;
            ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
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
                             "unscaled configuration selects full SetDIBitsToDevice with "
                             "default filter");
            }

            ScopedEnvironmentVariable filterEnvironment("CNA_GDI_PRESENT_FILTER");
            ScopedEnvironmentVariable dirtyEnvironment("CNA_GDI_DIRTY_PRESENTATION");
            ScopedEnvironmentVariable dwmEnvironment("CNA_GDI_DWM_FLUSH");
            ok &= Expect(filterEnvironment.Set(expectHalftone ? "nearest" : "halftone") &&
                             dirtyEnvironment.Set(expectDirty ? "0" : "1") &&
                             dwmEnvironment.Set("1"),
                         "test mutates all process settings after renderer construction");

            renderer.Present();
            ok &= Expect(renderer.DebugGetConfiguration() == expectedConfiguration,
                         "post-construction environment mutation cannot alter captured policy");
            ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                             telemetry.result.success &&
                             telemetry.stretchMode ==
                                 (expectHalftone ? kHalftoneStretchMode
                                                 : kColorOnColorStretchMode) &&
                             telemetry.plan.path ==
                                 (halftoneVariant
                                      ? GdiBlitPath::Stretch
                                      : (expectDirty ? GdiBlitPath::None
                                                     : GdiBlitPath::NativeFull)),
                         "Present uses its immutable filter and dirty policy snapshot");
        }

        {
            ScopedEnvironmentVariable filterEnvironment("CNA_GDI_PRESENT_FILTER");
            ScopedEnvironmentVariable dirtyEnvironment("CNA_GDI_DIRTY_PRESENTATION");
            ScopedEnvironmentVariable dwmEnvironment("CNA_GDI_DWM_FLUSH");
            ok &= Expect(filterEnvironment.Set("nearest") && dirtyEnvironment.Set("0") &&
                             dwmEnvironment.Set("1"),
                         "explicit-override test establishes contrary process settings");

            const GdiConfiguration overrideConfiguration{
                GdiPresentationFilter::Halftone, true, false};
            GdiRenderer renderer(
                CNA::Examples::SdlTestRendererArgs(
                    window, nullptr, nullptr, clientWidth, clientHeight,
                    CnaPresentationMode::NativeBackBuffer),
                overrideConfiguration);
            ok &= Expect(renderer.DebugGetConfiguration() == overrideConfiguration,
                         "typed test override bypasses every process setting");
            renderer.Clear(0.2f, 0.3f, 0.4f, 1.0f);
            renderer.Present();
            renderer.Present();

            GdiPresentationTelemetry telemetry;
            ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                             telemetry.result.success &&
                             telemetry.plan.path == GdiBlitPath::None &&
                             telemetry.stretchMode == kHalftoneStretchMode,
                         "typed override controls filter and no-damage presentation behavior");
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
