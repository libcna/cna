// SPDX-License-Identifier: MS-PL
// SKIA-161: real Ganesh/OpenGL default-framebuffer wrapping, flush/submit, swap, readback,
// resize, and destruction order -- proven with actual drawn-and-read-back pixels, not just
// "the surface constructs." This source only compiles its real assertions in a GANESH-mode
// build (CNA_SKIA_MODE_GANESH); see examples/skia_ganesh_mode_test.cpp for the RASTER-mode
// refusal proof SkiaGaneshSurface inherits unconditionally from SkiaGaneshContext.

#include "CNA/Internal/Renderers/Skia/SkiaGaneshSurface.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdint>
#include <string_view>

using CNA::Internal::Renderers::Skia::SkiaGaneshSurface;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;
using CNA::Examples::SdlTestSurface;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures;
    }
}

#if defined(CNA_SKIA_MODE_GANESH)

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <array>
#include <cmath>

namespace
{
    [[nodiscard]] std::array<std::uint8_t, 4> SamplePixel(const SkiaGaneshSurface& surface, int x, int y)
    {
        std::array<std::uint8_t, 4> pixel{};
        if (!surface.ReadPixels(x, y, 1, 1, pixel.data()))
            return {0, 0, 0, 0};
        return pixel;
    }

    [[nodiscard]] bool CloseEnough(std::uint8_t a, std::uint8_t b, int tolerance = 2)
    {
        return std::abs(static_cast<int>(a) - static_cast<int>(b)) <= tolerance;
    }

    [[nodiscard]] bool MatchesColor(const std::array<std::uint8_t, 4>& pixel,
                                     std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        return CloseEnough(pixel[0], r) && CloseEnough(pixel[1], g)
            && CloseEnough(pixel[2], b) && CloseEnough(pixel[3], a);
    }
}

int main(int argc, char** argv)
{
    // CTest always runs this with no arguments (hidden window, automated, exits immediately).
    // --visible is for a human developer to actually look at: a real, on-screen window showing
    // the exact same drawn pattern the automated checks below verify byte-for-byte, held on
    // screen for a few seconds at the end -- the "visible smoke" half of this task's acceptance
    // text, using the same binary rather than a second, separately maintained tool (mirroring
    // cna_demo_2d's own established --smoke N dual-purpose design).
    bool visible = false;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--visible")
            visible = true;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    const int windowSize = visible ? 480 : 64;
    SDL_Window* window = SDL_CreateWindow(
        "CNA Skia Ganesh backbuffer", windowSize, windowSize,
        SDL_WINDOW_OPENGL | (visible ? 0 : SDL_WINDOW_HIDDEN));
    if (!window)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SdlTestGlContext glContext(window);
    const auto makeArgs = [&]
    {
        return SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0,
            CNA::Internal::Renderers::CnaPresentationMode::NativeBackBuffer);
    };

    {
        SkiaGaneshSurface surface(makeArgs());
        Check(surface.Width() > 0 && surface.Height() > 0,
              "wrapping the default framebuffer reports a positive size");

        SkCanvas* canvas = surface.Canvas();
        Check(canvas != nullptr, "a real SkCanvas is available over the wrapped surface");

        // Pure, maximally distinct opaque colours: any R/B channel-order mistake in the GL
        // readback path would make blue read back as red (or vice versa), not just look "off."
        constexpr SkColor kBackground = SkColorSetARGB(255, 0, 0, 255);   // opaque blue
        constexpr SkColor kForeground = SkColorSetARGB(255, 255, 0, 0);   // opaque red

        canvas->clear(kBackground);
        SkPaint paint;
        paint.setColor(kForeground);
        // Only the top-left quadrant -- an asymmetric mark, not a full-surface clear, is what
        // actually exercises origin correctness: a top/bottom flip bug would move this mark to
        // the bottom-left quadrant on readback instead, while a full clear would look identical
        // either way.
        canvas->drawRect(SkRect::MakeXYWH(0, 0, surface.Width() / 2.0f, surface.Height() / 2.0f), paint);

        // Read back BEFORE swapping: SkSurface::readPixels() flushes pending Skia work itself,
        // but SDL_GL_SwapWindow's buffer swap leaves the (now back) buffer's contents undefined
        // on a double-buffered context -- reading after a swap would race a driver-dependent
        // undefined value, not what was actually drawn. Present() is exercised separately below,
        // once, specifically to prove the swap mechanism itself does not crash/fail.
        const auto topLeft = SamplePixel(surface, 2, 2);
        const auto topRight = SamplePixel(surface, surface.Width() - 3, 2);
        const auto bottomLeft = SamplePixel(surface, 2, surface.Height() - 3);
        const auto bottomRight = SamplePixel(surface, surface.Width() - 3, surface.Height() - 3);

        Check(MatchesColor(topLeft, 255, 0, 0, 255),
              "top-left quadrant reads back the drawn red rect (not the background)");
        Check(MatchesColor(topRight, 0, 0, 255, 255),
              "top-right quadrant reads back the blue background, undisturbed by the rect");
        Check(MatchesColor(bottomLeft, 0, 0, 255, 255),
              "bottom-left quadrant reads back the blue background -- proves the origin is not "
              "flipped (a flip would put the red rect here instead)");
        Check(MatchesColor(bottomRight, 0, 0, 255, 255),
              "bottom-right quadrant reads back the blue background");

        // A translucent overlay must actually blend (neither fully replace nor be discarded) --
        // proving alpha is genuinely interpreted through this path, not silently treated as 0 or
        // 255 regardless of the requested value. Still pre-swap, on top of the still-live canvas
        // content from above (no swap has happened yet, so nothing has become undefined).
        SkPaint translucent;
        translucent.setColor(SkColorSetARGB(128, 0, 255, 0)); // half-alpha green
        canvas->drawRect(SkRect::MakeXYWH(
            surface.Width() / 2.0f, surface.Height() / 2.0f,
            surface.Width() / 2.0f, surface.Height() / 2.0f), translucent);
        const auto blended = SamplePixel(surface, surface.Width() - 3, surface.Height() - 3);
        const bool isPureBlue = MatchesColor(blended, 0, 0, 255, 255);
        const bool isPureGreen = MatchesColor(blended, 0, 255, 0, 255);
        Check(!isPureBlue && !isPureGreen && blended[1] > 0 && blended[2] > 0,
              "a translucent draw actually blends with the background (neither fully replaced "
              "nor discarded), proving alpha is interpreted through this readback path");

        // Now prove the swap mechanism itself: flush/submit + SDL_GL_SwapWindow must not throw.
        surface.Present();

        // Resize: the real SDL window, not a caller-simulated event, exactly matching the
        // established real-window-resize precedent (examples/easygl_real_window_resize_test.cpp).
        const int originalWidth = surface.Width();
        const int originalHeight = surface.Height();
        SDL_SetWindowSize(window, originalWidth * 2, originalHeight + 40);
        SDL_SyncWindow(window); // blocks until the pending resize actually takes effect
        bool resized = false;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            SDL_PumpEvents();
            int w = 0, h = 0;
            if (SDL_GetWindowSizeInPixels(window, &w, &h) && (w != originalWidth || h != originalHeight))
            {
                resized = true;
                break;
            }
            SDL_Delay(50);
        }
        Check(resized, "the real SDL window actually resized (precondition for the next check)");

        surface.Resize(SdlTestSurface(window));
        Check(surface.Width() != originalWidth || surface.Height() != originalHeight,
              "SkiaGaneshSurface::Resize() rewraps the default framebuffer at its new size");

        // Fresh clear at the new size -- the prior frame's content is not assumed to survive the
        // rewrap (Resize() replaces the wrapped SkSurface entirely) or the earlier Present()'s
        // swap (undefined either way, per the note above).
        SkCanvas* resizedCanvas = surface.Canvas();
        resizedCanvas->clear(SkColorSetARGB(255, 0, 255, 0)); // opaque green
        const auto afterResize = SamplePixel(surface, 2, 2);
        Check(MatchesColor(afterResize, 0, 255, 0, 255),
              "the resized surface is genuinely drawable and readable at its new dimensions");
        surface.Present();

        // SKIA-162: DebugSimulateContextLossEXT() is a genuine GL context destroy+recreate
        // (mirroring EasyGLRenderer::DebugSimulateContextLoss()'s own real recreate, not a
        // faked one) -- three cycles in a row, proving the recreate path itself is repeatable and
        // leaves no stale GPU object behind each time, not just once.
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            surface.DebugSimulateContextLossEXT();
            SkCanvas* recoveredCanvas = surface.Canvas();
            const SkColor cycleColor = SkColorSetARGB(255, 255, 128, 0); // opaque orange
            recoveredCanvas->clear(cycleColor);
            const auto afterLoss = SamplePixel(surface, 2, 2);
            Check(MatchesColor(afterLoss, 255, 128, 0, 255),
                  "recovered surface is genuinely drawable and readable after a simulated "
                  "context loss (cycle)");
            surface.Present();
        }

        // Best-effort fullscreen: SDL_SetWindowFullscreen can fail or no-op under a headless/
        // virtual display (documented precedent: examples/easygl_fullscreen_field_test.cpp).
        // A failure here is not this code's fault and must not fail the test -- only verify the
        // resize mechanism still works correctly on the rare environment where it does succeed.
        if (SDL_SetWindowFullscreen(window, true))
        {
            SDL_SyncWindow(window);
            const int preFullscreenWidth = surface.Width();
            const int preFullscreenHeight = surface.Height();
            bool fullscreenResized = false;
            for (int attempt = 0; attempt < 20; ++attempt)
            {
                SDL_PumpEvents();
                int w = 0, h = 0;
                if (SDL_GetWindowSizeInPixels(window, &w, &h)
                    && (w != preFullscreenWidth || h != preFullscreenHeight))
                {
                    fullscreenResized = true;
                    break;
                }
                SDL_Delay(50);
            }
            if (fullscreenResized)
            {
                surface.Resize(SdlTestSurface(window));
                SkCanvas* fullscreenCanvas = surface.Canvas();
                fullscreenCanvas->clear(SkColorSetARGB(255, 200, 0, 200)); // opaque magenta
                const auto afterFullscreen = SamplePixel(surface, 2, 2);
                Check(MatchesColor(afterFullscreen, 200, 0, 200, 255),
                      "Resize() correctly rewraps after a real fullscreen-triggered drawable "
                      "size change");
                surface.Present();
            }
            else
            {
                std::printf("[INFO] fullscreen toggle reported success but the drawable size did "
                            "not change under this display -- skipping the fullscreen resize "
                            "check (not a failure; see examples/easygl_fullscreen_field_test.cpp)\n");
            }
            SDL_SetWindowFullscreen(window, false);
            SDL_SyncWindow(window);
        }
        else
        {
            std::printf("[INFO] SDL_SetWindowFullscreen failed (expected under a headless/virtual "
                        "display) -- skipping the fullscreen resize check, not a failure\n");
        }
    }
    // The destructor above must have released the wrapped surface and Ganesh context cleanly
    // (surface before context, matching SkiaGaneshSurface's own declared member order) before
    // reaching here; constructing a second, independent surface on the same window proves no
    // state leaked across instances.
    {
        SkiaGaneshSurface second(makeArgs());
        // A genuine mid-tone, not a pure primary: this is what first caught the sRGB/linear
        // colour-space bug below (WrapBackbuffer's original SkColorSpace::MakeSRGBLinear() choice
        // silently applied a gamma round-trip that pure 0/255 channel values cannot expose).
        second.Canvas()->clear(SkColorSetARGB(255, 128, 64, 200));
        const auto pixel = SamplePixel(second, 2, 2);
        Check(MatchesColor(pixel, 128, 64, 200, 255),
              "a second, independent SkiaGaneshSurface also wraps, draws, and reads back correctly "
              "(a genuine mid-tone colour, not just a pure primary)");
        second.Present();
    }

    if (visible)
    {
        // Hold a final, recognizable, freshly-redrawn pattern on screen for a human to actually
        // see -- a third SkiaGaneshSurface, independent of the two already destroyed above.
        SkiaGaneshSurface smoke(makeArgs());
        SkCanvas* canvas = smoke.Canvas();
        canvas->clear(SkColorSetARGB(255, 0, 0, 255));
        SkPaint paint;
        paint.setColor(SkColorSetARGB(255, 255, 0, 0));
        canvas->drawRect(SkRect::MakeXYWH(0, 0, smoke.Width() / 2.0f, smoke.Height() / 2.0f), paint);
        smoke.Present();

        const auto deadline = SDL_GetTicks() + 3000;
        while (SDL_GetTicks() < deadline)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {}
            SDL_Delay(16);
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}

#else

int main()
{
    Check(true,
          "RASTER-mode build: SkiaGaneshSurface's real backbuffer wrapping is not compiled here "
          "(no Ganesh/GL Skia symbol exists to test); its unconditional construction-time refusal "
          "is already proven by examples/skia_ganesh_mode_test.cpp");
    return failures == 0 ? 0 : 1;
}

#endif
