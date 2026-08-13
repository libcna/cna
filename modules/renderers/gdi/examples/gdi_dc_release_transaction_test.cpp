// SPDX-License-Identifier: MS-PL
// GDI-077: ReleaseDC is now part of the checked presentation transaction rather than a
// non-throwing RAII destructor whose result was discarded. A failed release must be visible in
// telemetry, must not let damage/the invalidation generation be acknowledged, and must never be
// attempted more than once for a given Present().

#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool SameRectangle(const Rectangle& a, const Rectangle& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
    }

    bool RunPresent(GdiRenderer& renderer, std::string* message)
    {
        try
        {
            renderer.Present();
        }
        catch (const std::runtime_error& error)
        {
            if (message)
                *message = error.what();
            return false;
        }
        return true;
    }

    bool ExerciseDcReleaseTransaction(SDL_Window* window)
    {
        bool ok = true;
        GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
            window, nullptr, nullptr, 16, 12, CnaPresentationMode::NativeBackBuffer));

        // Ordinary present establishes a clean baseline: no pending damage, generation
        // acknowledged.
        renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
        ok &= Expect(RunPresent(renderer, nullptr), "ordinary present succeeds");
        Rectangle ignored;
        bool ignoredFull = false;
        ok &= Expect(!renderer.DebugGetBackbufferDamage(ignored, ignoredFull),
                     "ordinary present commits and clears damage");

        // A real minimize/restore round-trip requests a retained no-draw repaint, so the watched
        // native invalidation generation is genuinely ahead before the injected failure below.
        ok &= Expect(SDL_ShowWindow(window) && SDL_SyncWindow(window),
                     "test window becomes visible for the minimize/restore round-trip");
        ok &= Expect(SDL_MinimizeWindow(window) && SDL_SyncWindow(window),
                     "native window enters the minimized lifecycle state");
        SDL_PumpEvents();
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
        ok &= Expect(SDL_RestoreWindow(window) && SDL_SyncWindow(window),
                     "native window completes the restore round-trip");
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        renderer.OnSurfaceInvalidated(SDL_GetWindowID(window));
        ok &= Expect(renderer.DebugIsNativeClientInvalidated(),
                     "real restore requests a retained repaint before the injected failure");

        renderer.Clear(0.4f, 0.5f, 0.6f, 1.0f);
        Rectangle damageBeforeFailure;
        bool fullyDirtyBeforeFailure = false;
        const bool damageValidBeforeFailure =
            renderer.DebugGetBackbufferDamage(damageBeforeFailure, fullyDirtyBeforeFailure);
        ok &= Expect(damageValidBeforeFailure, "clear leaves pending damage before the injection");

        renderer.DebugForceNextReleaseDcFailure();
        std::string failureMessage;
        const bool presentedOk = RunPresent(renderer, &failureMessage);
        ok &= Expect(!presentedOk &&
                         failureMessage.find("ReleaseDC") != std::string::npos &&
                         failureMessage.find("Win32 error") != std::string::npos,
                     "forced ReleaseDC failure surfaces as a diagnosed presentation failure");

        GdiPresentationTelemetry telemetry;
        ok &= Expect(renderer.DebugGetLastPresentationTelemetry(telemetry) &&
                         !telemetry.result.success &&
                         std::string(telemetry.result.operation) == "ReleaseDC",
                     "telemetry identifies ReleaseDC as the operation that failed");

        Rectangle damageAfterFailure;
        bool fullyDirtyAfterFailure = false;
        const bool damageValidAfterFailure =
            renderer.DebugGetBackbufferDamage(damageAfterFailure, fullyDirtyAfterFailure);
        ok &= Expect(damageValidAfterFailure == damageValidBeforeFailure &&
                         fullyDirtyAfterFailure == fullyDirtyBeforeFailure &&
                         SameRectangle(damageAfterFailure, damageBeforeFailure),
                     "a failed checked release conservatively retains pending damage");
        ok &= Expect(renderer.DebugIsNativeClientInvalidated(),
                     "a failed checked release does not acknowledge the invalidation generation");

        // Exactly one release attempt is made per Present() -- repeating the injected failure a
        // few times must never crash, double-release, or otherwise corrupt renderer state.
        for (int i = 0; i < 3; ++i)
        {
            renderer.Clear(0.1f, 0.1f * static_cast<float>(i), 0.2f, 1.0f);
            renderer.DebugForceNextReleaseDcFailure();
            ok &= Expect(!RunPresent(renderer, nullptr),
                         "repeated forced release failures behave deterministically");
        }

        // A subsequent ordinary present succeeds and fully commits, proving the renderer recovers.
        ok &= Expect(RunPresent(renderer, nullptr),
                     "a subsequent Present() succeeds after repeated injected failures");
        ok &= Expect(!renderer.DebugGetBackbufferDamage(damageAfterFailure, fullyDirtyAfterFailure),
                     "successful retry commits and clears retained damage");
        ok &= Expect(!renderer.DebugIsNativeClientInvalidated(),
                     "successful retry acknowledges the retained invalidation generation");
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
        "CNA GDI DC release transaction", 16, 12, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool ok = false;
    try
    {
        ok = ExerciseDcReleaseTransaction(window);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI DC release transaction test failed: %s\n", error.what());
        ok = false;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
