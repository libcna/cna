// SPDX-License-Identifier: MS-PL
// SKIA-107: keep runtime mode, capability diagnostics, alpha bytes and recovery coherent.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaStartupDiagnostic.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

using CNA::GraphicsCapability;
using CNA::Internal::Backends::BackendDeviceEvent;
using CNA::Internal::Backends::CnaPresentationMode;
using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::kSkiaStartupDiagnostic;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> ReadPixel(SkiaGraphicsBackend& backend)
    {
        std::array<std::uint8_t, 4> pixel{};
        backend.ReadBackbuffer(0, 0, 1, 1, pixel.data());
        return pixel;
    }

    [[nodiscard]] bool HasExactCapabilities(const SkiaGraphicsBackend& backend)
    {
        return !backend.SupportsCapability(GraphicsCapability::ThreeD)
            && !backend.SupportsCapability(GraphicsCapability::DepthStencilBuffer)
            && !backend.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing)
            && !backend.SupportsCapability(GraphicsCapability::MultipleRenderTargets)
            && !backend.SupportsCapability(GraphicsCapability::AnisotropicFiltering)
            && !backend.SupportsCapability(GraphicsCapability::WireFrame)
            && !backend.SupportsCapability(GraphicsCapability::OcclusionQuery)
            && !backend.SupportsCapability(GraphicsCapability::CustomEffects)
            && backend.SupportsCapability(GraphicsCapability::Texture3D);
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA Skia raster-mode coherence", 32, 24,
                                          SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::vector<BackendDeviceEvent> events;
    {
        SkiaGraphicsBackend backend(
            window, 8, 8, CnaPresentationMode::NativeBackBuffer, 0,
            [&events](BackendDeviceEvent event) { events.push_back(event); });

        const std::string_view report = backend.GetStartupDiagnosticEXT();
        Check(report == kSkiaStartupDiagnostic && report.find("surface=raster") != std::string_view::npos,
              "runtime diagnostic names the one selected raster mode exactly");
        Check(report.find("colour=RGBA_8888/premultiplied") != std::string_view::npos
                  && report.find("samples=0") != std::string_view::npos
                  && report.find("anisotropic filtering=unsupported") != std::string_view::npos,
              "runtime diagnostic describes the actual alpha, sample and filtering policy");
        Check(HasExactCapabilities(backend),
              "runtime capabilities agree with the raster diagnostic and transfer-only Texture3D");
        Check(SDL_GetRenderer(window) == backend.GetRendererInternal(),
              "the SDL renderer is presentation-only and remains owned by the Skia backend");

        // Components are chosen so conversion through 8-bit premultiplied storage round-trips
        // exactly. This observes the public backbuffer boundary rather than only SkSurface internals.
        backend.Clear(64.0f / 255.0f, 128.0f / 255.0f, 0.0f, 128.0f / 255.0f);
        const std::array<std::uint8_t, 4> expected{64, 128, 0, 128};
        Check(ReadPixel(backend) == expected,
              "semi-transparent raster clear normalizes back to exact straight RGBA8");

        backend.DebugSimulateContextLoss();
        Check(events == std::vector<BackendDeviceEvent>{BackendDeviceEvent::Resetting,
                                                        BackendDeviceEvent::Reset},
              "presentation recovery reports one ordered reset pair without fabricated device loss");
        Check(ReadPixel(backend) == expected,
              "presentation recovery preserves the CPU-owned premultiplied raster pixels");
        Check(HasExactCapabilities(backend) && backend.GetStartupDiagnosticEXT() == report,
              "recovery cannot change mode, capabilities or the immutable diagnostic");
        Check(SDL_GetRenderer(window) == backend.GetRendererInternal(),
              "recovery rebuilds only the owned SDL presenter and does not switch Skia modes");
    }

    Check(SDL_GetRenderer(window) == nullptr && SDL_GetWindowID(window) != 0,
          "raster-mode destruction releases the presenter and preserves the caller window");
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("=== %d/%d PASS ===\n", 9 - failures, 9);
    return failures == 0 ? 0 : 1;
}
