// SPDX-License-Identifier: MS-PL
// GDI-060: drawable-pixel sizing, SDL input-coordinate transforms, and window lifecycle.

#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Mouse;
using Microsoft::Xna::Framework::Input::TextInputEXT;

namespace
{
    struct WindowMetrics
    {
        int coordinateWidth = 0;
        int coordinateHeight = 0;
        int drawableWidth = 0;
        int drawableHeight = 0;
        int win32ClientWidth = 0;
        int win32ClientHeight = 0;
        float pixelDensity = 0.0f;
        float displayScale = 0.0f;
    };

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool NearlyEqual(float left, float right, float tolerance = 0.01f)
    {
        return std::fabs(left - right) <= tolerance;
    }

    bool QueryWindowMetrics(SDL_Window* window, WindowMetrics& metrics)
    {
        if (window == nullptr ||
            !SDL_GetWindowSize(window, &metrics.coordinateWidth, &metrics.coordinateHeight) ||
            !SDL_GetWindowSizeInPixels(window, &metrics.drawableWidth, &metrics.drawableHeight))
        {
            return false;
        }

        const HWND nativeWindow = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        RECT client{};
        if (nativeWindow == nullptr || !GetClientRect(nativeWindow, &client))
            return false;

        metrics.win32ClientWidth = static_cast<int>(client.right - client.left);
        metrics.win32ClientHeight = static_cast<int>(client.bottom - client.top);
        metrics.pixelDensity = SDL_GetWindowPixelDensity(window);
        metrics.displayScale = SDL_GetWindowDisplayScale(window);
        return metrics.coordinateWidth > 0 && metrics.coordinateHeight > 0 &&
               metrics.drawableWidth > 0 && metrics.drawableHeight > 0 &&
               metrics.win32ClientWidth > 0 && metrics.win32ClientHeight > 0;
    }

    bool ExpectMetricAgreement(const WindowMetrics& metrics, const char* context)
    {
        char sizeMessage[192]{};
        std::snprintf(sizeMessage, sizeof(sizeMessage),
                      "%s uses SDL drawable pixels equal to the Win32 GDI client", context);
        bool ok = Expect(metrics.drawableWidth == metrics.win32ClientWidth &&
                             metrics.drawableHeight == metrics.win32ClientHeight,
                         sizeMessage);

        char densityMessage[192]{};
        std::snprintf(densityMessage, sizeof(densityMessage),
                      "%s reports positive SDL pixel-density and display-scale metrics", context);
        ok &= Expect(std::isfinite(metrics.pixelDensity) && metrics.pixelDensity > 0.0f &&
                         std::isfinite(metrics.displayScale) && metrics.displayScale > 0.0f,
                     densityMessage);
        return ok;
    }

    bool ExerciseExternalWindowLifecycle(SDL_Window* window)
    {
        bool ok = true;
        Mouse::setWindowHandleProperty(0);
        TextInputEXT::setWindowHandleProperty(0);
        const std::uint32_t windowId = SDL_GetWindowID(window);

        {
            PresentationParameters parameters;
            parameters.setBackBufferWidthProperty(31);
            parameters.setBackBufferHeightProperty(23);
            parameters.setDeviceWindowHandleProperty(
                reinterpret_cast<PresentationParameters::IntPtr>(window));

            GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                                  GraphicsProfile::Reach, parameters);
            const std::uintptr_t expectedHandle = reinterpret_cast<std::uintptr_t>(window);
            ok &= Expect(Mouse::getWindowHandleProperty() == expectedHandle &&
                             TextInputEXT::getWindowHandleProperty() == expectedHandle,
                         "caller-provided SDL window is published to mouse and text input");
        }

        ok &= Expect(Mouse::getWindowHandleProperty() == 0 &&
                         TextInputEXT::getWindowHandleProperty() == 0,
                     "destroying an attached GraphicsDevice clears matching input handles");
        ok &= Expect(windowId != 0 && SDL_GetWindowFromID(windowId) == window,
                     "GraphicsDevice does not destroy its caller-provided SDL window");
        return ok;
    }

    bool ExerciseOddResizes(GdiRenderer& renderer, SDL_Window* window)
    {
        constexpr std::array<std::array<int, 2>, 3> sizes{{
            {{31, 23}},
            {{47, 29}},
            {{63, 37}},
        }};

        bool ok = true;
        for (const auto& size : sizes)
        {
            const bool resized = SDL_SetWindowSize(window, size[0], size[1]) &&
                                 SDL_SyncWindow(window);
            SDL_PumpEvents();
            renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));

            WindowMetrics metrics;
            const bool queried = QueryWindowMetrics(window, metrics);
            char resizeMessage[160]{};
            std::snprintf(resizeMessage, sizeof(resizeMessage),
                          "odd resize %dx%d reaches SDL window-coordinate space",
                          size[0], size[1]);
            ok &= Expect(resized && queried &&
                             metrics.coordinateWidth == size[0] &&
                             metrics.coordinateHeight == size[1],
                         resizeMessage);
            if (!queried)
                continue;

            ok &= ExpectMetricAgreement(metrics, resizeMessage);
            int logicalWidth = 0;
            int logicalHeight = 0;
            renderer.GetViewportSize(logicalWidth, logicalHeight);
            const int expectedWidth = std::max(1, static_cast<int>(std::lround(
                static_cast<double>(metrics.drawableWidth) * 17.0 /
                metrics.drawableHeight)));
            char logicalMessage[192]{};
            std::snprintf(logicalMessage, sizeof(logicalMessage),
                          "odd resize %dx%d resolves fixed-height logical dimensions",
                          size[0], size[1]);
            ok &= Expect(logicalWidth == expectedWidth && logicalHeight == 17, logicalMessage);

            renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
            renderer.Present();
            GdiPresentationTelemetry telemetry;
            char destinationMessage[192]{};
            std::snprintf(destinationMessage, sizeof(destinationMessage),
                          "odd resize %dx%d presents to SDL drawable-pixel dimensions",
                          size[0], size[1]);
            ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                             telemetry.result.success &&
                             telemetry.plan.destination.width == metrics.drawableWidth &&
                             telemetry.plan.destination.height == metrics.drawableHeight,
                         destinationMessage);
        }
        return ok;
    }

    bool ExerciseAllTransforms(GdiRenderer& renderer, SDL_Window* window)
    {
        struct ModeCase
        {
            CnaPresentationMode mode;
            const char* name;
        };
        constexpr ModeCase modes[]{
            {CnaPresentationMode::Letterbox, "Letterbox"},
            {CnaPresentationMode::Overscan, "Overscan"},
            {CnaPresentationMode::Stretch, "Stretch"},
            {CnaPresentationMode::NativeBackBuffer, "NativeBackBuffer"},
            {CnaPresentationMode::FixedHeightDynamicWidth, "FixedHeightDynamicWidth"},
        };

        WindowMetrics metrics;
        if (!QueryWindowMetrics(window, metrics))
            return Expect(false, "presentation-mode transform test can query window metrics");

        bool ok = true;
        for (const ModeCase& mode : modes)
        {
            renderer.SetPresentationMode(static_cast<int>(mode.mode));
            int logicalWidth = 0;
            int logicalHeight = 0;
            renderer.GetViewportSize(logicalWidth, logicalHeight);

            const float expectedX = logicalWidth * 0.5f;
            const float expectedY = logicalHeight * 0.5f;
            float windowX = -1.0f;
            float windowY = -1.0f;
            float logicalX = -1.0f;
            float logicalY = -1.0f;
            const bool roundTrip = renderer.TransformLogicalToWindow(
                                       expectedX, expectedY, windowX, windowY) &&
                                   renderer.TransformWindowToLogical(
                                       windowX, windowY, logicalX, logicalY);
            char roundTripMessage[192]{};
            std::snprintf(roundTripMessage, sizeof(roundTripMessage),
                          "%s center round-trips through SDL window coordinates", mode.name);
            ok &= Expect(roundTrip && NearlyEqual(logicalX, expectedX) &&
                             NearlyEqual(logicalY, expectedY),
                         roundTripMessage);

            const GdiPresentationRect destination = CalculateGdiPresentationDestination(
                mode.mode, metrics.drawableWidth, metrics.drawableHeight,
                logicalWidth, logicalHeight);
            float uncoveredPixelX = -1.0f;
            float uncoveredPixelY = -1.0f;
            if (destination.x > 0)
            {
                uncoveredPixelX = destination.x * 0.5f;
                uncoveredPixelY = metrics.drawableHeight * 0.5f;
            }
            else if (destination.y > 0)
            {
                uncoveredPixelX = metrics.drawableWidth * 0.5f;
                uncoveredPixelY = destination.y * 0.5f;
            }
            else if (destination.x + destination.width < metrics.drawableWidth)
            {
                uncoveredPixelX = (destination.x + destination.width +
                                   metrics.drawableWidth) * 0.5f;
                uncoveredPixelY = metrics.drawableHeight * 0.5f;
            }
            else if (destination.y + destination.height < metrics.drawableHeight)
            {
                uncoveredPixelX = metrics.drawableWidth * 0.5f;
                uncoveredPixelY = (destination.y + destination.height +
                                   metrics.drawableHeight) * 0.5f;
            }

            if (uncoveredPixelX >= 0.0f)
            {
                const float uncoveredWindowX = uncoveredPixelX *
                    metrics.coordinateWidth / metrics.drawableWidth;
                const float uncoveredWindowY = uncoveredPixelY *
                    metrics.coordinateHeight / metrics.drawableHeight;
                char barMessage[192]{};
                std::snprintf(barMessage, sizeof(barMessage),
                              "%s rejects an SDL coordinate outside presented content", mode.name);
                ok &= Expect(!renderer.TransformWindowToLogical(
                                 uncoveredWindowX, uncoveredWindowY, logicalX, logicalY),
                             barMessage);
            }
        }
        return ok;
    }

    bool ExerciseFullscreenRoundTrip(GdiRenderer& renderer, SDL_Window* window)
    {
        if (!SDL_SetWindowFullscreen(window, true))
        {
            std::printf("[SKIP] fullscreen transition unavailable in this display session: %s\n",
                        SDL_GetError());
            return true;
        }

        bool ok = Expect(SDL_SyncWindow(window),
                         "fullscreen entry synchronizes the SDL window");
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        WindowMetrics fullscreenMetrics;
        ok &= Expect(QueryWindowMetrics(window, fullscreenMetrics),
                     "fullscreen state exposes positive SDL and Win32 client metrics");
        if (fullscreenMetrics.drawableWidth > 0)
            ok &= ExpectMetricAgreement(fullscreenMetrics, "fullscreen state");

        renderer.SetPresentationMode(
            static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth));
        renderer.Clear(0.2f, 0.3f, 0.4f, 1.0f);
        renderer.Present();
        int logicalWidth = 0;
        int logicalHeight = 0;
        renderer.GetViewportSize(logicalWidth, logicalHeight);
        float windowX = 0.0f;
        float windowY = 0.0f;
        float logicalX = 0.0f;
        float logicalY = 0.0f;
        ok &= Expect(renderer.TransformLogicalToWindow(
                         logicalWidth * 0.5f, logicalHeight * 0.5f, windowX, windowY) &&
                         renderer.TransformWindowToLogical(
                             windowX, windowY, logicalX, logicalY) &&
                         NearlyEqual(logicalX, logicalWidth * 0.5f) &&
                         NearlyEqual(logicalY, logicalHeight * 0.5f),
                     "fullscreen center coordinate survives a logical/SDL round-trip");

        const bool leftFullscreen = SDL_SetWindowFullscreen(window, false);
        ok &= Expect(leftFullscreen && SDL_SyncWindow(window),
                     "fullscreen exit restores the windowed SDL state");
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        WindowMetrics restoredMetrics;
        ok &= Expect(QueryWindowMetrics(window, restoredMetrics),
                     "windowed state exposes positive metrics after fullscreen exit");
        if (restoredMetrics.drawableWidth > 0)
            ok &= ExpectMetricAgreement(restoredMetrics, "restored windowed state");
        return ok;
    }

    bool ExerciseMinimizeRetention(GdiRenderer& renderer, SDL_Window* window)
    {
        bool ok = Expect(SDL_SetWindowSize(window, 63, 37) && SDL_SyncWindow(window),
                         "retention test restores its final odd window size");
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        renderer.SetPresentationMode(
            static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth));
        renderer.Clear(0.125f, 0.25f, 0.5f, 1.0f);
        renderer.Present();

        int widthBefore = 0;
        int heightBefore = 0;
        renderer.GetViewportSize(widthBefore, heightBefore);
        std::array<std::uint8_t, 4> pixelBefore{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelBefore.data());

        ok &= Expect(SDL_MinimizeWindow(window) && SDL_SyncWindow(window),
                     "window enters the minimized state for retention coverage");
        SDL_PumpEvents();
        int widthMinimized = 0;
        int heightMinimized = 0;
        renderer.GetViewportSize(widthMinimized, heightMinimized);
        std::array<std::uint8_t, 4> pixelMinimized{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelMinimized.data());
        if (widthMinimized != widthBefore || heightMinimized != heightBefore ||
            pixelMinimized != pixelBefore)
        {
            std::printf("[INFO] minimize retention before=%dx%d rgba=%u,%u,%u,%u; "
                        "minimized=%dx%d rgba=%u,%u,%u,%u\n",
                        widthBefore, heightBefore,
                        static_cast<unsigned>(pixelBefore[0]),
                        static_cast<unsigned>(pixelBefore[1]),
                        static_cast<unsigned>(pixelBefore[2]),
                        static_cast<unsigned>(pixelBefore[3]),
                        widthMinimized, heightMinimized,
                        static_cast<unsigned>(pixelMinimized[0]),
                        static_cast<unsigned>(pixelMinimized[1]),
                        static_cast<unsigned>(pixelMinimized[2]),
                        static_cast<unsigned>(pixelMinimized[3]));
        }
        ok &= Expect(widthMinimized == widthBefore && heightMinimized == heightBefore &&
                         pixelMinimized == pixelBefore,
                     "minimize neither reallocates nor erases the retained logical backbuffer");

        ok &= Expect(SDL_RestoreWindow(window) && SDL_SyncWindow(window),
                     "window restores after retained-backbuffer minimize coverage");
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        int widthRestored = 0;
        int heightRestored = 0;
        renderer.GetViewportSize(widthRestored, heightRestored);
        std::array<std::uint8_t, 4> pixelRestored{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelRestored.data());
        if (widthRestored != widthBefore || heightRestored != heightBefore ||
            pixelRestored != pixelBefore)
        {
            std::printf("[INFO] restore retention before=%dx%d rgba=%u,%u,%u,%u; "
                        "restored=%dx%d rgba=%u,%u,%u,%u\n",
                        widthBefore, heightBefore,
                        static_cast<unsigned>(pixelBefore[0]),
                        static_cast<unsigned>(pixelBefore[1]),
                        static_cast<unsigned>(pixelBefore[2]),
                        static_cast<unsigned>(pixelBefore[3]),
                        widthRestored, heightRestored,
                        static_cast<unsigned>(pixelRestored[0]),
                        static_cast<unsigned>(pixelRestored[1]),
                        static_cast<unsigned>(pixelRestored[2]),
                        static_cast<unsigned>(pixelRestored[3]));
        }
        ok &= Expect(widthRestored == widthBefore && heightRestored == heightBefore &&
                         pixelRestored == pixelBefore,
                     "restore preserves the retained logical dimensions and pixel contents");
        return ok;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "CNA GDI window metrics", 31, 23,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
            SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        bool ok = ExerciseExternalWindowLifecycle(window);
        ok &= Expect(SDL_ShowWindow(window) && SDL_SyncWindow(window),
                     "metrics test shows and synchronizes its native Win32 client");
        SDL_PumpEvents();

        GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
            window, nullptr, nullptr, 23, 17,
            CnaPresentationMode::FixedHeightDynamicWidth));
        WindowMetrics initialMetrics;
        ok &= Expect(QueryWindowMetrics(window, initialMetrics),
                     "initial window exposes SDL and Win32 metrics");
        if (initialMetrics.drawableWidth > 0)
            ok &= ExpectMetricAgreement(initialMetrics, "initial window");

        ok &= ExerciseOddResizes(renderer, window);
        ok &= ExerciseAllTransforms(renderer, window);
        ok &= ExerciseFullscreenRoundTrip(renderer, window);
        ok &= ExerciseMinimizeRetention(renderer, window);
        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI window metrics test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
