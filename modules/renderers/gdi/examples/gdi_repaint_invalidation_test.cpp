// SPDX-License-Identifier: MS-PL
// GDI-052: retained-client repaint invalidation independent of Win32 GetUpdateRect lifetime.

#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;

namespace
{
    constexpr int kColorOnColorStretchMode = 3; // Win32 COLORONCOLOR.

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool PushWindowEvent(SDL_Window* window, std::uint32_t type)
    {
        SDL_Event event{};
        event.type = type;
        event.window.windowID = SDL_GetWindowID(window);
        return SDL_PushEvent(&event);
    }

    struct DirtyEnvironment
    {
        DirtyEnvironment() { _putenv_s("CNA_GDI_DIRTY_PRESENTATION", "1"); }
        ~DirtyEnvironment() { _putenv_s("CNA_GDI_DIRTY_PRESENTATION", ""); }
    };
}

int main()
{
    DirtyEnvironment dirtyEnvironment;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI repaint invalidation", 32, 24,
                                          SDL_WINDOW_HIDDEN);
    SDL_Window* other = SDL_CreateWindow("CNA GDI unrelated window", 8, 8,
                                         SDL_WINDOW_HIDDEN);
    if (window == nullptr || other == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        if (other != nullptr) SDL_DestroyWindow(other);
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
            window, nullptr, nullptr, 32, 24, CnaPresentationMode::Stretch));
        bool ok = true;

        renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
        renderer.Present();
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "successful initial present acknowledges native client generation");
        GdiPresentationTelemetry telemetry;
        ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                         telemetry.plan.path == GdiBlitPath::NativeFull &&
                         telemetry.result.success,
                     "renderer telemetry records the initial full native path");

        renderer.Present();
        ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                         telemetry.plan.path == GdiBlitPath::None &&
                         telemetry.result.success,
                     "renderer telemetry records a no-damage dirty-present skip");

        renderer.DebugResetBackbufferDamage();
        ok &= Expect(PushWindowEvent(other, SDL_EVENT_WINDOW_EXPOSED),
                     "unrelated synthetic expose event was queued");
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(other));
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "another platform window cannot invalidate this GDI client");

        ok &= Expect(PushWindowEvent(window, SDL_EVENT_WINDOW_EXPOSED),
                     "synthetic expose event was queued");
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
        ok &= Expect(renderer.DebugIsNativeClientInvalidated(),
                     "platform exposure records a repaint even if WM_PAINT was already validated");
        Rectangle damage;
        bool fullyDirty = false;
        ok &= Expect(!renderer.DebugGetBackbufferDamage(damage, fullyDirty),
                     "native expose is retained separately from new CPU drawing damage");
        renderer.Present();
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "no-draw present repairs and acknowledges an exposed client");

        constexpr std::array<std::uint32_t, 5> invalidatingEvents{
            SDL_EVENT_WINDOW_RESTORED,
            SDL_EVENT_WINDOW_RESIZED,
            SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
            SDL_EVENT_WINDOW_DISPLAY_CHANGED,
            SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED,
        };
        for (const std::uint32_t eventType : invalidatingEvents)
        {
            ok &= Expect(PushWindowEvent(window, eventType),
                         "synthetic lifecycle event was queued");
            renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
            ok &= Expect(renderer.DebugIsNativeClientInvalidated(),
                         "lifecycle event requests a complete retained-client repaint");
            renderer.Present();
            ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                         "successful lifecycle repaint acknowledges only its captured generation");
        }

        SDL_Event foreground{};
        foreground.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        ok &= Expect(SDL_PushEvent(&foreground), "foreground event was queued");
        renderer.OnSurfaceInvalidated(0);
        ok &= Expect(renderer.DebugIsNativeClientInvalidated(),
                     "process foreground transition invalidates the retained GDI client");
        renderer.Present();
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "foreground repaint completes through a no-draw dirty present");

        // Exercise the real Win32 paint lifecycle behind F3, not only injected events. The
        // platform-mapped exposure remains authoritative after GetUpdateRect is empty.
        ok &= Expect(SDL_ShowWindow(window) && SDL_SyncWindow(window),
                     "native repaint test shows and synchronizes its Win32 client");
        SDL_PumpEvents();
        renderer.Present(); // acknowledge SHOWN/EXPOSED before the explicit WM_PAINT below.
        const HWND nativeWindow = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        ok &= Expect(nativeWindow != nullptr && InvalidateRect(nativeWindow, nullptr, FALSE) &&
                         UpdateWindow(nativeWindow),
                     "native InvalidateRect/UpdateWindow dispatches WM_PAINT");
        SDL_PumpEvents();
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
        ok &= Expect(GetUpdateRect(nativeWindow, nullptr, FALSE) == FALSE &&
                         renderer.DebugIsNativeClientInvalidated(),
                     "platform-mapped WM_PAINT still leaves a repaint generation pending");
        renderer.Present();
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "no-draw present repairs the real validated WM_PAINT client");

        int logicalWidthBeforeMinimize = 0;
        int logicalHeightBeforeMinimize = 0;
        renderer.GetViewportSize(logicalWidthBeforeMinimize, logicalHeightBeforeMinimize);
        ok &= Expect(SDL_MinimizeWindow(window) && SDL_SyncWindow(window),
                     "native window enters the minimized lifecycle state");
        SDL_PumpEvents();
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
        int logicalWidthWhileMinimized = 0;
        int logicalHeightWhileMinimized = 0;
        renderer.GetViewportSize(logicalWidthWhileMinimized, logicalHeightWhileMinimized);
        ok &= Expect(renderer.DebugIsNativeClientInvalidated() &&
                         logicalWidthWhileMinimized == logicalWidthBeforeMinimize &&
                         logicalHeightWhileMinimized == logicalHeightBeforeMinimize,
                     "minimize retains logical storage and leaves native repaint pending");
        ok &= Expect(SDL_RestoreWindow(window) && SDL_SyncWindow(window),
                     "native window completes the restore round-trip");
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
        ok &= Expect(renderer.DebugIsNativeClientInvalidated(),
                     "real restore requests a retained no-draw repaint");
        renderer.Present();
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "real restore repaint acknowledges the latest lifecycle generation");
        (void)SDL_HideWindow(window);

        const ImageData image{1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}};
        std::unique_ptr<ITextureRenderer> texture = renderer.CreateTexture(image);
        std::unique_ptr<ISpriteBatchRenderer> sprites = renderer.CreateSpriteBatch();
        sprites->Begin();
        sprites->Draw(*texture, Rectangle(4, 7, 3, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites->End();

        Rectangle beforeFailure;
        bool beforeFailureFull = true;
        ok &= Expect(renderer.DebugGetBackbufferDamage(beforeFailure, beforeFailureFull) &&
                         !beforeFailureFull && beforeFailure.Y > 0,
                     "non-zero-Y sprite prepares a partial dirty-band present");
        renderer.DebugForceNextDibBlitFailure();
        bool injectedFailureThrew = false;
        std::string injectedFailureMessage;
        try
        {
            renderer.Present();
        }
        catch (const std::runtime_error& error)
        {
            injectedFailureThrew = true;
            injectedFailureMessage = error.what();
        }
        ok &= Expect(injectedFailureThrew &&
                         injectedFailureMessage.find("SetDIBitsToDevice(dirty band)") !=
                             std::string::npos &&
                         injectedFailureMessage.find("Win32 error") != std::string::npos,
                     "zero-line DIB failure reports operation and Win32 error context");
        ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                         telemetry.plan.path == GdiBlitPath::NativeDirty &&
                         !telemetry.result.success &&
                         telemetry.plan.source.x == beforeFailure.X &&
                         telemetry.plan.source.y == beforeFailure.Y &&
                         telemetry.plan.source.width == beforeFailure.Width &&
                         telemetry.plan.source.height == beforeFailure.Height,
                     "renderer telemetry exposes selected dirty path, bounds, and failure");

        Rectangle afterFailure;
        bool afterFailureFull = true;
        ok &= Expect(renderer.DebugGetBackbufferDamage(afterFailure, afterFailureFull) &&
                         !afterFailureFull &&
                         afterFailure.X == beforeFailure.X &&
                         afterFailure.Y == beforeFailure.Y &&
                         afterFailure.Width == beforeFailure.Width &&
                         afterFailure.Height == beforeFailure.Height,
                     "failed partial present retains the complete pending damage rectangle");
        renderer.Present();
        ok &= Expect(!renderer.DebugGetBackbufferDamage(afterFailure, afterFailureFull),
                     "successful retry commits and clears retained presentation damage");

        ok &= Expect(SDL_SetWindowSize(window, 40, 30),
                     "scaled-present test resized the native client");
        ok &= Expect(SDL_SyncWindow(window),
                     "scaled-present test synchronized the native resize");
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        renderer.Clear(0.2f, 0.3f, 0.4f, 1.0f);
        renderer.DebugForceNextDibBlitFailure();
        bool stretchFailureThrew = false;
        std::string stretchFailureMessage;
        try
        {
            renderer.Present();
        }
        catch (const std::runtime_error& error)
        {
            stretchFailureThrew = true;
            stretchFailureMessage = error.what();
        }
        ok &= Expect(stretchFailureThrew &&
                         stretchFailureMessage.find("StretchDIBits") != std::string::npos &&
                         stretchFailureMessage.find("Win32 error") != std::string::npos,
                     "zero-line StretchDIBits result is a diagnosed presentation failure");
        ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                         telemetry.plan.path == GdiBlitPath::Stretch &&
                         !telemetry.result.success &&
                         telemetry.stretchMode == kColorOnColorStretchMode,
                     "renderer telemetry exposes scaled path, default filter, and failure");
        ok &= Expect(renderer.DebugGetBackbufferDamage(afterFailure, afterFailureFull) &&
                         afterFailureFull && renderer.DebugIsNativeClientInvalidated(),
                     "failed scaled present retains full CPU and native-client damage");
        renderer.Present();
        ok &= Expect(!renderer.DebugGetBackbufferDamage(afterFailure, afterFailureFull) &&
                         !renderer.DebugIsNativeClientInvalidated(),
                     "successful scaled retry commits both pending damage sources");

        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI repaint invalidation test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(other);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
