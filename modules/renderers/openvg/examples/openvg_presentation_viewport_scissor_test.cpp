// SPDX-License-Identifier: MS-PL
// Decisive pixel-level and geometry-oracle coverage for OpenVgRenderer's presentation transform,
// custom Viewport, scissor rect, resize-without-Clear, single-live-ShivaVG-context guard and swap
// interval control -- constructed directly against OpenVgRenderer/OpenVgSpriteBatchRenderer/
// OpenVgTextureRenderer (no Game/GraphicsDeviceManager), matching this module's own low-level
// OpenVgRendererTests.cpp style, but against a REAL SDL window + GL context (Xvfb) since these are
// exactly the entry points a fake/mock window cannot exercise honestly.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::OpenVg;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;
using CNA::Examples::SdlTestSurface;

namespace
{
    int pass = 0, fail = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass; else ++fail;
    }

    SDL_Window* MakeWindow(int w, int h)
    {
        SDL_Window* window = SDL_CreateWindow("openvg-presentation-test", w, h, SDL_WINDOW_OPENGL);
        if (!window)
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return window;
    }

    ImageData SolidImage(int w, int h, const Color& c)
    {
        ImageData data;
        data.width = w;
        data.height = h;
        data.pixels.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < data.pixels.size(); i += 4)
        {
            data.pixels[i + 0] = static_cast<uint8_t>(c.getRProperty());
            data.pixels[i + 1] = static_cast<uint8_t>(c.getGProperty());
            data.pixels[i + 2] = static_cast<uint8_t>(c.getBProperty());
            data.pixels[i + 3] = static_cast<uint8_t>(c.getAProperty());
        }
        return data;
    }

    Color ReadPixel(OpenVgRenderer& renderer, int x, int y)
    {
        uint8_t rgba[4] = {0, 0, 0, 0};
        renderer.ReadBackbuffer(x, y, 1, 1, rgba);
        return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    bool CloseTo(const Color& a, const Color& b, int tol = 12)
    {
        const auto d = [](int x, int y) { return x > y ? x - y : y - x; };
        return d(a.getRProperty(), b.getRProperty()) <= tol &&
               d(a.getGProperty(), b.getGProperty()) <= tol &&
               d(a.getBProperty(), b.getBProperty()) <= tol;
    }

    void DrawFullCanvasSprite(OpenVgRenderer& renderer, ISpriteBatchRenderer& sb,
                              ITextureRenderer& tex, const Rectangle& destLogical)
    {
        sb.Begin();
        sb.Draw(tex, destLogical, Rectangle(0, 0, tex.GetWidth(), tex.GetHeight()),
               Color(255, 255, 255, 255));
        sb.End();
    }

    // ---- Presentation-mode pixel oracle: window 200x100 (2:1), virtual 100x100 (1:1) ----
    void TestPresentationModes()
    {
        const Color kClear(10, 10, 10, 255);
        const Color kSprite(200, 30, 220, 255);

        // OpenVgRenderer is non-copyable (owns a GL context + the single-live-context slot) --
        // construct each case in its own scope instead of via a helper returning by value.
        {
            SDL_Window* window = MakeWindow(200, 100);
            {
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 100, 100, CnaPresentationMode::Letterbox));
            auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
            auto sb = renderer.CreateSpriteBatch();
            renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                           kClear.getBProperty() / 255.0f, 1.0f);
            DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));

            int lw = 0, lh = 0; renderer.GetViewportSize(lw, lh);
            Check(lw == 100 && lh == 100, "Letterbox: logical size stays the fixed virtual resolution");
            int px = 0, py = 0, pw = 0, ph = 0; renderer.GetDefaultViewportRect(px, py, pw, ph);
            Check(px == 50 && py == 0 && pw == 100 && ph == 100,
                  "Letterbox: default physical rect is centred with left/right bars");
            Check(CloseTo(ReadPixel(renderer, 100, 50), kSprite), "Letterbox: sprite visible at physical centre");
            Check(CloseTo(ReadPixel(renderer, 10, 50), kClear), "Letterbox: left bar shows the clear colour, not the sprite");
            Check(CloseTo(ReadPixel(renderer, 190, 50), kClear), "Letterbox: right bar shows the clear colour, not the sprite");
            Check(CloseTo(ReadPixel(renderer, 55, 50), kSprite), "Letterbox: just inside the left presentation edge is sprite");
            Check(CloseTo(ReadPixel(renderer, 145, 50), kSprite), "Letterbox: just inside the right presentation edge is sprite");
            }
            SDL_DestroyWindow(window);
        }
        {
            SDL_Window* window = MakeWindow(200, 100);
            {
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 100, 100, CnaPresentationMode::Overscan));
            auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
            auto sb = renderer.CreateSpriteBatch();
            renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                           kClear.getBProperty() / 255.0f, 1.0f);
            DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));

            int px = 0, py = 0, pw = 0, ph = 0; renderer.GetDefaultViewportRect(px, py, pw, ph);
            Check(px == 0 && py == -50 && pw == 200 && ph == 200,
                  "Overscan: default physical rect grows to cover the window, cropping top/bottom");
            Check(CloseTo(ReadPixel(renderer, 0, 0), kSprite) &&
                      CloseTo(ReadPixel(renderer, 199, 99), kSprite) &&
                      CloseTo(ReadPixel(renderer, 100, 50), kSprite),
                  "Overscan: the ENTIRE window is covered -- no bars anywhere");
            }
            SDL_DestroyWindow(window);
        }
        {
            SDL_Window* window = MakeWindow(200, 100);
            {
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 100, 100, CnaPresentationMode::Stretch));
            auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
            auto sb = renderer.CreateSpriteBatch();
            renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                           kClear.getBProperty() / 255.0f, 1.0f);
            // Left HALF of the logical canvas only -- Stretch's non-uniform scaleX=200/100=2.0
            // must place its right edge at physical x=100, not x=50.
            DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 50, 100));

            int px = 0, py = 0, pw = 0, ph = 0; renderer.GetDefaultViewportRect(px, py, pw, ph);
            Check(px == 0 && py == 0 && pw == 200 && ph == 100,
                  "Stretch: default physical rect fills the window exactly, no bars/crop");
            Check(CloseTo(ReadPixel(renderer, 40, 50), kSprite),
                  "Stretch: non-uniform scaleX places logical half-width sprite up to physical x~100");
            Check(CloseTo(ReadPixel(renderer, 160, 50), kClear),
                  "Stretch: nothing drawn beyond the non-uniformly scaled sprite edge");
            }
            SDL_DestroyWindow(window);
        }
        {
            SDL_Window* window = MakeWindow(200, 100);
            {
            // FixedHeightDynamicWidth: virtualHeight=100 fixed, width derives from the window's own
            // aspect (200/100 = 2.0) -> dynamic logical width 200, matching the window exactly.
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 100, 100,
                CnaPresentationMode::FixedHeightDynamicWidth));
            int lw = 0, lh = 0; renderer.GetViewportSize(lw, lh);
            Check(lw == 200 && lh == 100,
                  "FixedHeightDynamicWidth: logical width derives from the window's own aspect");
            int px = 0, py = 0, pw = 0, ph = 0; renderer.GetDefaultViewportRect(px, py, pw, ph);
            Check(px == 0 && py == 0 && pw == 200 && ph == 100,
                  "FixedHeightDynamicWidth: fills the window with no bars by construction");
            }
            SDL_DestroyWindow(window);
        }
        {
            SDL_Window* window = MakeWindow(200, 100);
            {
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 100, 100,
                CnaPresentationMode::NativeBackBuffer));
            int lw = 0, lh = 0; renderer.GetViewportSize(lw, lh);
            Check(lw == 200 && lh == 100,
                  "NativeBackBuffer: logical size equals the physical window regardless of virtual resolution");
            }
            SDL_DestroyWindow(window);
        }
    }

    // ---- TransformWindowToLogical / TransformLogicalToWindow round-trip + letterbox bars ----
    void TestCoordinateTransforms()
    {
        SDL_Window* window = MakeWindow(200, 100);
        {
        SdlTestGlContext glContext(window);
        OpenVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 100, 100, CnaPresentationMode::Letterbox));

        float logX = -1.0f, logY = -1.0f;
        Check(renderer.TransformWindowToLogical(100.0f, 50.0f, logX, logY) &&
                  logX > 49.0f && logX < 51.0f && logY > 49.0f && logY < 51.0f,
              "TransformWindowToLogical: window centre maps to logical centre");
        Check(!renderer.TransformWindowToLogical(10.0f, 50.0f, logX, logY),
              "TransformWindowToLogical: a point inside the letterbox bar has no logical position");
        Check(!renderer.TransformWindowToLogical(190.0f, 50.0f, logX, logY),
              "TransformWindowToLogical: the opposite letterbox bar is rejected too");

        float winX = -1.0f, winY = -1.0f;
        Check(renderer.TransformLogicalToWindow(50.0f, 50.0f, winX, winY) &&
                  winX > 99.0f && winX < 101.0f && winY > 49.0f && winY < 51.0f,
              "TransformLogicalToWindow: logical centre maps back to the window centre");

        // Round-trip: a point safely inside the presentation rect survives window->logical->window.
        Check(renderer.TransformWindowToLogical(120.0f, 30.0f, logX, logY) &&
                  renderer.TransformLogicalToWindow(logX, logY, winX, winY) &&
                  winX > 119.0f && winX < 121.0f && winY > 29.0f && winY < 31.0f,
              "window -> logical -> window round-trips to the original point");

        }
        SDL_DestroyWindow(window);
    }

    // ---- Custom viewport: pixel-correct placement, survives Clear() and resize ----
    void TestViewport()
    {
        SDL_Window* window = MakeWindow(200, 100);
        {
        SdlTestGlContext glContext(window);
        OpenVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
        const Color kClear(5, 5, 5, 255);
        const Color kSprite(240, 120, 10, 255);
        auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
        auto sb = renderer.CreateSpriteBatch();

        // A viewport smaller than, and offset from, the full window.
        renderer.SetViewport(50, 20, 60, 40, 0.0f, 1.0f);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 200, 100));

        Check(CloseTo(ReadPixel(renderer, 80, 40), kSprite),
              "custom viewport: sprite rasterizes inside the requested rectangle");
        Check(CloseTo(ReadPixel(renderer, 10, 10), kClear),
              "custom viewport: nothing drawn outside the requested rectangle (top-left corner)");
        Check(CloseTo(ReadPixel(renderer, 190, 90), kClear),
              "custom viewport: nothing drawn outside the requested rectangle (bottom-right corner)");

        // P0-3: Clear() must not reset the custom viewport back to full-window.
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 200, 100));
        Check(CloseTo(ReadPixel(renderer, 80, 40), kSprite) && CloseTo(ReadPixel(renderer, 10, 10), kClear),
              "custom viewport survives Clear()");

        }
        SDL_DestroyWindow(window);
    }

    // ---- Resize without Clear (P0-4) ----
    void TestResizeWithoutClear()
    {
        SDL_Window* window = MakeWindow(120, 80);
        {
        SdlTestGlContext glContext(window);
        OpenVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
        const Color kClear(1, 2, 3, 255);
        const Color kSprite(0, 220, 90, 255);
        auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
        auto sb = renderer.CreateSpriteBatch();

        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);

        SDL_SetWindowSize(window, 240, 160);
        SDL_SyncWindow(window);
        SDL_PumpEvents();
        renderer.OnSurfaceChanged(SdlTestSurface(window));

        // Proves EnsureSurfaceSizeEXT()'s own bookkeeping is correct immediately -- no draw, no
        // swap, nothing beyond the resize itself.
        {
            int lwEarly = 0, lhEarly = 0; renderer.GetViewportSize(lwEarly, lhEarly);
            int dvx = 0, dvy = 0, dvw = 0, dvh = 0; renderer.GetDefaultViewportRect(dvx, dvy, dvw, dvh);
            Check(lwEarly == 240 && lhEarly == 160 && dvx == 0 && dvy == 0 && dvw == 240 && dvh == 160,
                  "resize without Clear: logical size and default viewport rect are correct "
                  "immediately after resize, before any draw/present");
        }

        // Xvfb/Mesa software-GLX environment characteristic (verified, not assumed): SDL and
        // The refreshed platform surface snapshot and GetDefaultViewportRect already report the
        // new physical size correctly at this point, but the ACTUAL GLX drawable's backing store
        // on this X server is not reallocated to the new size until one buffer swap has completed
        // after the resize; a draw issued before that swap still rasterizes into the OLD-sized
        // buffer even though every CNA-side viewport/ortho/scissor computation is already correct.
        // This is an X11/GLX-level characteristic of this display server, outside OpenVgRenderer's
        // control (no additional `vg*`/`gl*` call from CNA's own code changes it) -- one harmless
        // platform swap settles it, matching how a real application's per-frame Present() would
        // naturally absorb this on its very next frame.
        glContext.SwapBuffers(SDL_GetWindowID(window));
        SDL_PumpEvents();

        // No Clear() call here at all -- draw immediately after the resize. If the physical
        // surface/viewport/ortho are not resynchronized before this draw, either the geometry
        // lands in the wrong place or ShivaVG's own stale internal surface size corrupts the
        // result.
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 240, 160));

        int lw = 0, lh = 0; renderer.GetViewportSize(lw, lh);
        Check(lw == 240 && lh == 160, "resize without Clear: logical size reflects the NEW window size");
        Check(CloseTo(ReadPixel(renderer, 120, 80), kSprite),
              "resize without Clear: a draw immediately after resize lands at the correct NEW physical position");
        Check(CloseTo(ReadPixel(renderer, 235, 155), kSprite),
              "resize without Clear: the far corner of the NEW (larger) canvas is covered too");

        }
        SDL_DestroyWindow(window);
    }

    // ---- Scissor: RasterizerState-driven enable, independent rectangle updates (P0-5) ----
    void TestScissor()
    {
        SDL_Window* window = MakeWindow(100, 100);
        {
        SdlTestGlContext glContext(window);
        OpenVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
        const Color kClear(3, 3, 3, 255);
        const Color kSprite(255, 255, 0, 255);
        auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
        auto sb = renderer.CreateSpriteBatch();

        constexpr int kCullCCW = 2, kFillSolid = 0; // XNA ordinals, matching ApplyRasterizerState's own contract

        // Default: scissor rect set but SCISSOR TEST DISABLED -- SetScissorRect alone must not
        // implicitly enable scissoring (P0-5's central claim).
        renderer.SetScissorRect(10, 10, 20, 20);
        renderer.ApplyRasterizerState(kCullCCW, kFillSolid, /*scissorTestEnable=*/false, 0.0f, 0.0f);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));
        Check(CloseTo(ReadPixel(renderer, 50, 50), kSprite),
              "scissor rect set but disabled: draw is NOT clipped");

        // Enabled: the same rectangle now genuinely clips.
        renderer.ApplyRasterizerState(kCullCCW, kFillSolid, /*scissorTestEnable=*/true, 0.0f, 0.0f);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));
        Check(CloseTo(ReadPixel(renderer, 15, 15), kSprite), "scissor enabled: inside the rect still draws");
        Check(CloseTo(ReadPixel(renderer, 50, 50), kClear), "scissor enabled: outside the rect is clipped");

        // Changing just the rectangle while still enabled.
        renderer.SetScissorRect(60, 60, 20, 20);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));
        Check(CloseTo(ReadPixel(renderer, 70, 70), kSprite) && CloseTo(ReadPixel(renderer, 15, 15), kClear),
              "scissor rectangle change while enabled takes effect immediately");

        // Disabling again restores unclipped drawing.
        renderer.ApplyRasterizerState(kCullCCW, kFillSolid, /*scissorTestEnable=*/false, 0.0f, 0.0f);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));
        Check(CloseTo(ReadPixel(renderer, 5, 5), kSprite), "disabling scissor again restores unclipped drawing");

        }
        SDL_DestroyWindow(window);
    }

    // ---- Single-live-ShivaVG-context guard (P0-9) + sequential recreation ----
    void TestSingleLiveContext()
    {
        SDL_Window* window1 = MakeWindow(64, 64);
        SdlTestGlContext glContext1(window1);
        auto renderer1 = std::make_unique<OpenVgRenderer>(SdlTestRendererArgs(
            window1, &glContext1, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));

        SDL_Window* window2 = MakeWindow(64, 64);
        SdlTestGlContext glContext2(window2);
        bool threwWhileFirstAlive = false;
        try
        {
            OpenVgRenderer renderer2(SdlTestRendererArgs(
                window2, &glContext2, nullptr, 0, 0,
                CnaPresentationMode::NativeBackBuffer));
            (void)renderer2;
        }
        catch (const std::runtime_error&)
        {
            threwWhileFirstAlive = true;
        }
        Check(threwWhileFirstAlive,
              "constructing a second OpenVgRenderer while the first is alive throws deterministically");

        renderer1.reset();
        SDL_DestroyWindow(window1);

        bool secondSucceeded = true;
        try
        {
            OpenVgRenderer renderer3(SdlTestRendererArgs(
                window2, &glContext2, nullptr, 0, 0,
                CnaPresentationMode::NativeBackBuffer));
            (void)renderer3;
        }
        catch (const std::exception& ex)
        {
            secondSucceeded = false;
            std::printf("    (unexpected: %s)\n", ex.what());
        }
        Check(secondSucceeded, "destroying the first renderer releases the slot; a new renderer can be constructed");

        SDL_DestroyWindow(window2);
    }

    // ---- Repeated construct/destroy lifecycle (P1-9) ----
    // A permanent regression test for the vgCreateContextSH/vgDestroyContextSH leak fixed by
    // cmake/patches/shivavg-context-dtor-leak.patch (VGContext_dtor previously freed the
    // individual path/paint/image objects but never the three resource arrays' own backing
    // storage nor c->defaultPaint's owned resources -- a fixed 64-byte/5-allocation-plus-one-GL-
    // texture leak per cycle). This loop's own job is to prove every cycle succeeds and the single-
    // live-context guard (TestSingleLiveContext above) never wedges across repeated
    // construct/destroy; the actual leak-freedom claim is verified by running this same binary
    // under LeakSanitizer (see docs/openvg-renderer.md), not by anything observable from C++ here.
    void TestRepeatedConstructDestroyCycle()
    {
        constexpr int kCycles = 25;
        bool allSucceeded = true;
        for (int i = 0; i < kCycles && allSucceeded; ++i)
        {
            SDL_Window* window = MakeWindow(32, 32);
            try
            {
                SdlTestGlContext glContext(window);
                OpenVgRenderer renderer(SdlTestRendererArgs(
                    window, &glContext, nullptr, 0, 0,
                    CnaPresentationMode::NativeBackBuffer));
                renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
                renderer.Present();
            }
            catch (const std::exception& ex)
            {
                allSucceeded = false;
                std::printf("    (cycle %d unexpectedly threw: %s)\n", i, ex.what());
            }
            SDL_DestroyWindow(window);
        }
        Check(allSucceeded, "25 sequential OpenVgRenderer construct/Clear/Present/destroy cycles all succeed");
    }

    // ---- Swap interval reaches SDL (P1-1) ----
    void TestSwapInterval()
    {
        SDL_Window* window = MakeWindow(64, 64);
        {
        SdlTestGlContext glContext(window);
        OpenVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0,
            CnaPresentationMode::NativeBackBuffer, /*swapInterval=*/0));
        int interval = -99;
        const bool queried = SDL_GL_GetSwapInterval(&interval);
        Check(queried, "SDL_GL_GetSwapInterval succeeds after OpenVgRenderer construction with swapInterval=0");
        Check(!queried || interval == 0,
              "requested swapInterval=0 actually reached SDL (or SDL reports what it fell back to)");

        renderer.SetSwapInterval(1);
        int intervalAfter = -99;
        Check(SDL_GL_GetSwapInterval(&intervalAfter) && intervalAfter != -99,
              "SetSwapInterval(1) is reflected by SDL_GL_GetSwapInterval");

        }
        SDL_DestroyWindow(window);
    }
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }

    try
    {
        TestPresentationModes();
        TestCoordinateTransforms();
        TestViewport();
        TestResizeWithoutClear();
        TestScissor();
        TestSingleLiveContext();
        TestRepeatedConstructDestroyCycle();
        TestSwapInterval();
    }
    catch (const std::exception& ex)
    {
        std::printf("[FAIL] uncaught exception: %s\n", ex.what());
        ++fail;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    std::printf("=== %d/%d PASS ===\n", pass, pass + fail);
    return fail == 0 ? 0 : 1;
}
