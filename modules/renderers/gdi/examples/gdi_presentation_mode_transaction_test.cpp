// SPDX-License-Identifier: MS-PL
// GDI-075: a valid presentation-mode ordinal can still derive a logical size that exceeds the
// framebuffer's axis or byte budget. The mode/size/damage transition must commit or roll back as
// one operation rather than leaving presentationMode_ pointed at a mode that keeps failing.
//
// The bug and its fix live entirely in GdiRenderer::SetPresentationMode, exercised here
// directly. Microsoft::Xna::Framework::Graphics::GraphicsDevice::SetPresentationMode(int) is a
// private, single-line, stateless forward (`if (renderer_) renderer_->SetPresentationMode(mode);`)
// reachable only through GraphicsDeviceManager, which in this CNA renderer requires a live Game
// (GraphicsDeviceManager::CreateDevice() throws otherwise) -- so it adds no state of its own for
// this renderer-level regression test to duplicate.

#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    template<typename Callback>
    bool ExpectArgumentOutOfRange(Callback&& callback, const char* message)
    {
        try
        {
            callback();
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            return Expect(true, message);
        }
        catch (...)
        {
            return Expect(false, message);
        }
        return Expect(false, message);
    }

    // A skewed real drawable aspect combined with a modest fixed virtual height derives a width
    // far beyond the 16384-axis ceiling once FixedHeightDynamicWidth is requested, without the
    // fixed height itself ever exceeding any per-axis limit.
    bool ForceSkewedAspect(SDL_Window* window)
    {
        const bool resized = SDL_SetWindowSize(window, 4000, 10) && SDL_SyncWindow(window);
        SDL_PumpEvents();
        return resized;
    }

    bool ExerciseDirectRenderer(SDL_Window* window)
    {
        bool ok = true;
        GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
            window, nullptr, nullptr, 40, 100, CnaPresentationMode::Letterbox));

        renderer.Clear(0.2f, 0.4f, 0.6f, 1.0f);
        renderer.Present();
        renderer.DebugResetBackbufferDamage();

        int widthBefore = 0, heightBefore = 0;
        renderer.GetViewportSize(widthBefore, heightBefore);
        std::array<std::uint8_t, 4> pixelBefore{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelBefore.data());
        Rectangle damageBefore;
        bool fullyDirtyBefore = false;
        const bool damageValidBefore =
            renderer.DebugGetBackbufferDamage(damageBefore, fullyDirtyBefore);

        ok &= Expect(widthBefore == 40 && heightBefore == 100,
                     "renderer starts from a valid retained Letterbox framebuffer");

        ok &= Expect(ForceSkewedAspect(window),
                     "test window resizes to an extreme drawable aspect ratio");
        renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));

        ok &= ExpectArgumentOutOfRange(
            [&] {
                renderer.SetPresentationMode(
                    static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth));
            },
            "switching to FixedHeightDynamicWidth against an impossible aspect throws");

        // GDI-075: the failed switch must not leave presentationMode_ pointed at
        // FixedHeightDynamicWidth -- every later call would otherwise recompute the same
        // oversized logical size and keep throwing instead of continuing with Letterbox.
        int widthAfter = 0, heightAfter = 0;
        bool viewportThrew = false;
        try
        {
            renderer.GetViewportSize(widthAfter, heightAfter);
        }
        catch (...)
        {
            viewportThrew = true;
        }
        ok &= Expect(!viewportThrew && widthAfter == widthBefore && heightAfter == heightBefore,
                     "rejected mode switch rolls back to the old mode's dimensions");

        std::array<std::uint8_t, 4> pixelAfter{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelAfter.data());
        ok &= Expect(pixelAfter == pixelBefore,
                     "rejected mode switch leaves retained pixels untouched");

        Rectangle damageAfter;
        bool fullyDirtyAfter = false;
        const bool damageValidAfter =
            renderer.DebugGetBackbufferDamage(damageAfter, fullyDirtyAfter);
        ok &= Expect(damageValidAfter == damageValidBefore &&
                         fullyDirtyAfter == fullyDirtyBefore &&
                         damageAfter.X == damageBefore.X && damageAfter.Y == damageBefore.Y &&
                         damageAfter.Width == damageBefore.Width &&
                         damageAfter.Height == damageBefore.Height,
                     "rejected mode switch leaves damage tracking untouched");

        bool presentThrew = false;
        try
        {
            renderer.Present();
        }
        catch (...)
        {
            presentThrew = true;
        }
        ok &= Expect(!presentThrew, "a subsequent Present() succeeds after the rejected switch");

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

    bool ok = true;
    SDL_Window* window = SDL_CreateWindow(
        "CNA GDI presentation mode transaction", 40, 100,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    try
    {
        ok &= ExerciseDirectRenderer(window);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI presentation mode transaction test failed: %s\n",
                     error.what());
        ok = false;
    }
    SDL_DestroyWindow(window);

    SDL_Quit();
    return ok ? 0 : 1;
}
