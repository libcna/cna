// SPDX-License-Identifier: MS-PL
// SKIA-13/SKIA-14: all raster presentation mappings, coordinate transforms, and a real resize.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Examples::SdlTestRendererArgs;
using CNA::Examples::SdlTestSurfacePresenter;

namespace
{
    constexpr int kVirtualWidth = 40;
    constexpr int kVirtualHeight = 30;
    constexpr float kTolerance = 0.08f;
    int failures = 0;

    void Check(bool pass, const std::string& label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass)
            ++failures;
    }

    [[nodiscard]] bool Near(float actual, float expected, float tolerance = kTolerance)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    [[nodiscard]] bool QuerySizes(SDL_Window* window,
                                  int& windowWidth, int& windowHeight,
                                  int& outputWidth, int& outputHeight)
    {
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        return windowWidth > 0 && windowHeight > 0
            && SDL_GetRenderOutputSize(SDL_GetRenderer(window), &outputWidth, &outputHeight)
            && outputWidth > 0 && outputHeight > 0;
    }

    void CheckRoundTrip(SkiaRenderer& renderer, float logicalX, float logicalY,
                        const std::string& label)
    {
        float windowX = 0.0f;
        float windowY = 0.0f;
        float roundTripX = 0.0f;
        float roundTripY = 0.0f;
        Check(renderer.TransformLogicalToWindow(logicalX, logicalY, windowX, windowY)
                  && renderer.TransformWindowToLogical(windowX, windowY, roundTripX, roundTripY)
                  && Near(roundTripX, logicalX) && Near(roundTripY, logicalY),
              label + " coordinate round trip is exact");
    }

    void CheckMode(SkiaRenderer& renderer, CnaPresentationMode mode,
                   float expectedScaleX, float expectedScaleY,
                   float expectedOriginX, float expectedOriginY,
                   const char* name)
    {
        renderer.SetPresentationMode(static_cast<int>(mode));

        int logicalWidth = 0;
        int logicalHeight = 0;
        renderer.GetViewportSize(logicalWidth, logicalHeight);
        Check(logicalWidth == kVirtualWidth && logicalHeight == kVirtualHeight,
              std::string(name) + " restores the requested 40x30 raster surface");

        float originX = 0.0f;
        float originY = 0.0f;
        float stepX = 0.0f;
        float stepXY = 0.0f;
        float stepYX = 0.0f;
        float stepY = 0.0f;
        const bool transformed = renderer.TransformLogicalToWindow(0.0f, 0.0f, originX, originY)
            && renderer.TransformLogicalToWindow(10.0f, 0.0f, stepX, stepXY)
            && renderer.TransformLogicalToWindow(0.0f, 10.0f, stepYX, stepY);
        Check(transformed && Near(originX, expectedOriginX) && Near(originY, expectedOriginY)
                  && Near((stepX - originX) / 10.0f, expectedScaleX)
                  && Near((stepY - originY) / 10.0f, expectedScaleY)
                  && Near(stepXY, originY) && Near(stepYX, originX),
              std::string(name) + " uses the expected scale and centred offset");
        CheckRoundTrip(renderer, 7.25f, 19.5f, name);

        const std::array<std::uint8_t, 4> expected{
            static_cast<std::uint8_t>(51), static_cast<std::uint8_t>(102),
            static_cast<std::uint8_t>(153), static_cast<std::uint8_t>(255)};
        std::array<std::uint8_t, 4> actual{};
        renderer.Clear(0.2f, 0.4f, 0.6f, 1.0f);
        renderer.ReadBackbuffer(kVirtualWidth - 1, kVirtualHeight - 1, 1, 1, actual.data());
        renderer.Present();
        Check(actual == expected, std::string(name) + " preserves exact raster clear/readback/present");
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "CNA Skia presentation modes", 120, 60, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    try
    {
        SdlTestSurfacePresenter presenter(window);
        SkiaRenderer renderer(SdlTestRendererArgs(
            window, nullptr, &presenter, kVirtualWidth, kVirtualHeight,
            CnaPresentationMode::FixedHeightDynamicWidth, 0));
        int windowWidth = 0;
        int windowHeight = 0;
        int outputWidth = 0;
        int outputHeight = 0;
        Check(QuerySizes(window, windowWidth, windowHeight, outputWidth, outputHeight),
              "SDL reports positive window and renderer-output dimensions");

        const float outputToWindowX = static_cast<float>(windowWidth) / outputWidth;
        const float outputToWindowY = static_cast<float>(windowHeight) / outputHeight;
        const float letterboxScale = std::min(
            static_cast<float>(windowWidth) / kVirtualWidth,
            static_cast<float>(windowHeight) / kVirtualHeight);
        const float overscanScale = std::max(
            static_cast<float>(windowWidth) / kVirtualWidth,
            static_cast<float>(windowHeight) / kVirtualHeight);

        CheckMode(renderer, CnaPresentationMode::Letterbox,
                  letterboxScale, letterboxScale,
                  (windowWidth - kVirtualWidth * letterboxScale) * 0.5f,
                  (windowHeight - kVirtualHeight * letterboxScale) * 0.5f,
                  "Letterbox");
        CheckMode(renderer, CnaPresentationMode::Overscan,
                  overscanScale, overscanScale,
                  (windowWidth - kVirtualWidth * overscanScale) * 0.5f,
                  (windowHeight - kVirtualHeight * overscanScale) * 0.5f,
                  "Overscan");
        CheckMode(renderer, CnaPresentationMode::Stretch,
                  static_cast<float>(windowWidth) / kVirtualWidth,
                  static_cast<float>(windowHeight) / kVirtualHeight,
                  0.0f, 0.0f, "Stretch");
        CheckMode(renderer, CnaPresentationMode::NativeBackBuffer,
                  outputToWindowX, outputToWindowY, 0.0f, 0.0f, "NativeBackBuffer");

        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth));
        int dynamicWidth = 0;
        int dynamicHeight = 0;
        renderer.GetViewportSize(dynamicWidth, dynamicHeight);
        const int expectedDynamicWidth = static_cast<int>(
            static_cast<double>(outputWidth) * kVirtualHeight / outputHeight + 0.5);
        Check(dynamicWidth == expectedDynamicWidth && dynamicHeight == kVirtualHeight,
              "FixedHeightDynamicWidth derives the raster width from renderer output aspect");
        CheckRoundTrip(renderer, dynamicWidth * 0.25f, kVirtualHeight * 0.75f,
                       "FixedHeightDynamicWidth before resize");

        const int resizedWindowWidth = windowWidth + 64;
        Check(SDL_SetWindowSize(window, resizedWindowWidth, windowHeight),
              "SDL accepts a real window resize after presentation-mode switches");

        bool resizeObserved = false;
        int resizedOutputWidth = 0;
        int resizedOutputHeight = 0;
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            SDL_PumpEvents();
            int currentWindowWidth = 0;
            int currentWindowHeight = 0;
            SDL_GetWindowSize(window, &currentWindowWidth, &currentWindowHeight);
            if (currentWindowWidth == resizedWindowWidth && currentWindowHeight == windowHeight
                && SDL_GetRenderOutputSize(SDL_GetRenderer(window),
                                           &resizedOutputWidth, &resizedOutputHeight)
                && resizedOutputWidth > 0 && resizedOutputHeight > 0
                && resizedOutputWidth != outputWidth)
            {
                resizeObserved = true;
                break;
            }
            SDL_Delay(10);
        }
        Check(resizeObserved, "renderer output observes the real window resize");

        renderer.GetViewportSize(dynamicWidth, dynamicHeight);
        const int expectedResizedDynamicWidth = resizeObserved
            ? static_cast<int>(static_cast<double>(resizedOutputWidth) * kVirtualHeight
                               / resizedOutputHeight + 0.5)
            : -1;
        Check(resizeObserved && dynamicWidth == expectedResizedDynamicWidth
                  && dynamicWidth != expectedDynamicWidth && dynamicHeight == kVirtualHeight,
              "real resize reallocates the CPU raster to the new fixed-height dynamic width");
        CheckRoundTrip(renderer, dynamicWidth * 0.5f, kVirtualHeight * 0.5f,
                       "FixedHeightDynamicWidth after resize");

        bool invalidRejected = false;
        try
        {
            renderer.SetPresentationMode(99);
        }
        catch (const std::out_of_range&)
        {
            invalidRejected = true;
        }
        int widthAfterReject = 0;
        int heightAfterReject = 0;
        renderer.GetViewportSize(widthAfterReject, heightAfterReject);
        Check(invalidRejected && widthAfterReject == dynamicWidth && heightAfterReject == dynamicHeight,
              "invalid presentation mode is rejected without changing the live raster");

        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
        renderer.GetViewportSize(widthAfterReject, heightAfterReject);
        Check(widthAfterReject == kVirtualWidth && heightAfterReject == kVirtualHeight,
              "leaving dynamic mode after resize restores the preferred virtual width");
    }
    catch (const std::exception& exception)
    {
        Check(false, std::string("unexpected exception: ") + exception.what());
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
