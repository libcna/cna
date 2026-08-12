// SPDX-License-Identifier: MS-PL
// SKIA-8: zero/minimized output, repeated physical resize, and presenter recovery.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::Skia::SkiaRenderer;

namespace
{
    constexpr int kVirtualWidth = 48;
    constexpr int kVirtualHeight = 24;
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures;
    }

    [[nodiscard]] bool Same(const std::array<std::uint8_t, 4>& pixel,
                            std::array<std::uint8_t, 4> expected)
    {
        return pixel == expected;
    }

    [[nodiscard]] bool ReadPixel(SkiaRenderer& renderer, int x, int y,
                                 std::array<std::uint8_t, 4>& pixel)
    {
        try
        {
            renderer.ReadBackbuffer(x, y, 1, 1, pixel.data());
            return true;
        }
        catch (const std::exception& exception)
        {
            std::printf("       readback exception: %s\n", exception.what());
            return false;
        }
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
        "CNA Skia window lifecycle", 96, 64, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    try
    {
        SkiaRenderer renderer(window, kVirtualWidth, kVirtualHeight,
                                    CnaPresentationMode::FixedHeightDynamicWidth, 0);
        int initialOutputWidth = 0;
        int initialOutputHeight = 0;
        const bool initialOutput = SDL_GetRenderOutputSize(
            SDL_GetRenderer(window), &initialOutputWidth, &initialOutputHeight);
        int logicalWidth = 0;
        int logicalHeight = 0;
        renderer.GetViewportSize(logicalWidth, logicalHeight);
        const int expectedInitialWidth = initialOutput && initialOutputHeight > 0
            ? static_cast<int>(static_cast<double>(initialOutputWidth) * kVirtualHeight
                               / initialOutputHeight + 0.5)
            : kVirtualWidth;
        Check(initialOutput && logicalWidth == expectedInitialWidth
                  && logicalHeight == kVirtualHeight,
              "initial fixed-height raster follows the real renderer output");

        renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        std::array<std::uint8_t, 4> pixel{};
        Check(ReadPixel(renderer, logicalWidth - 1, logicalHeight - 1, pixel)
                  && Same(pixel, {255, 0, 0, 255}),
              "initial raster contains an exact preserved sentinel");

        bool negativeOverrideRejected = false;
        try
        {
            renderer.DebugSetPresentationOutputSizeEXT(-1, 0);
        }
        catch (const std::out_of_range&)
        {
            negativeOverrideRejected = true;
        }
        int unchangedWidth = 0;
        int unchangedHeight = 0;
        renderer.GetViewportSize(unchangedWidth, unchangedHeight);
        Check(negativeOverrideRejected && unchangedWidth == logicalWidth
                  && unchangedHeight == logicalHeight,
              "invalid debug output dimensions reject without changing the live surface");

        renderer.DebugSetPresentationOutputSizeEXT(0, 0);
        int zeroWidth = 0;
        int zeroHeight = 0;
        renderer.GetViewportSize(zeroWidth, zeroHeight);
        Check(zeroWidth == logicalWidth && zeroHeight == logicalHeight,
              "0x0 minimized output preserves the last valid CPU surface dimensions");
        renderer.Present();
        pixel.fill(0);
        Check(ReadPixel(renderer, zeroWidth - 1, zeroHeight - 1, pixel)
                  && Same(pixel, {255, 0, 0, 255}),
              "Present during 0x0 output preserves raster contents");

        const bool hidden = SDL_HideWindow(window) && SDL_SyncWindow(window);
        bool hiddenPresent = true;
        try
        {
            renderer.Present();
        }
        catch (const std::exception& exception)
        {
            hiddenPresent = false;
            std::printf("       hidden Present exception: %s\n", exception.what());
        }
        Check(hidden && hiddenPresent, "hidden/minimized window accepts a no-loss Present");
        Check(SDL_ShowWindow(window) && SDL_SyncWindow(window),
              "hidden window returns to a synchronized visible state");

        renderer.DebugSetPresentationOutputSizeEXT(200, 100);
        renderer.GetViewportSize(logicalWidth, logicalHeight);
        Check(logicalWidth == 48 && logicalHeight == kVirtualHeight,
              "restored non-zero output recalculates and reallocates dynamic width");
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.Present();
        pixel.fill(0);
        Check(ReadPixel(renderer, logicalWidth - 1, logicalHeight - 1, pixel)
                  && Same(pixel, {0, 255, 0, 255}),
              "restored output clears, presents, and reads the replacement raster exactly");
        renderer.DebugClearPresentationOutputSizeEXT();

        bool everyResizeMatched = true;
        bool everyResizeRendered = true;
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            const int requestedWidth = 104 + cycle * 11;
            const int requestedHeight = 58 + (cycle % 3) * 9;
            if (!SDL_SetWindowSize(window, requestedWidth, requestedHeight)
                || !SDL_SyncWindow(window))
            {
                everyResizeMatched = false;
                break;
            }
            SDL_PumpEvents();

            int outputWidth = 0;
            int outputHeight = 0;
            if (!SDL_GetRenderOutputSize(SDL_GetRenderer(window), &outputWidth, &outputHeight)
                || outputWidth <= 0 || outputHeight <= 0)
            {
                everyResizeMatched = false;
                break;
            }
            renderer.GetViewportSize(logicalWidth, logicalHeight);
            const int expectedWidth = static_cast<int>(
                static_cast<double>(outputWidth) * kVirtualHeight / outputHeight + 0.5);
            everyResizeMatched = everyResizeMatched && logicalWidth == expectedWidth
                && logicalHeight == kVirtualHeight;

            const float red = static_cast<float>(cycle + 1) / 8.0f;
            renderer.Clear(red, 0.0f, 1.0f - red, 1.0f);
            renderer.Present();
            pixel.fill(0);
            everyResizeRendered = everyResizeRendered
                && ReadPixel(renderer, logicalWidth - 1, logicalHeight - 1, pixel)
                && pixel[3] == 255;
        }
        Check(everyResizeMatched,
              "eight real window resizes keep fixed-height dynamic raster dimensions exact");
        Check(everyResizeRendered,
              "every resized raster clears, presents, and reads its far corner safely");

        bool everyRecoveryPreserved = true;
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        for (int cycle = 0; cycle < 4; ++cycle)
        {
            renderer.DebugSimulateContextLoss();
            pixel.fill(0);
            everyRecoveryPreserved = everyRecoveryPreserved
                && ReadPixel(renderer, logicalWidth / 2, logicalHeight / 2, pixel)
                && Same(pixel, {0, 0, 255, 255});
            renderer.Present();
        }
        Check(everyRecoveryPreserved,
              "four presenter-loss recoveries preserve the live CPU raster and Present path");
    }
    catch (const std::exception& exception)
    {
        std::printf("[FAIL] unexpected exception: %s\n", exception.what());
        ++failures;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("=== %d/%d PASS ===\n", 12 - failures, 12);
    return failures == 0 ? 0 : 1;
}
