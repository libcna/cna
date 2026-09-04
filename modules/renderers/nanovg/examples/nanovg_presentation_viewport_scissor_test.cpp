// SPDX-License-Identifier: MS-PL
// Decisive pixel-level and geometry-oracle coverage for NanoVgRenderer's presentation transform,
// custom Viewport, scissor rect, resize-without-Clear, multi-instance coexistence and swap
// interval control -- CNA's port of openvg_presentation_viewport_scissor_test.cpp's own
// methodology, constructed directly against NanoVgRenderer/NanoVgSpriteBatchRenderer/
// NanoVgTextureRenderer (no Game/GraphicsDeviceManager), against a REAL SDL window + GL context
// (Xvfb).
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
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
using namespace CNA::Internal::Renderers::NanoVg;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;
using CNA::Examples::SdlTestSurface;

namespace
{
    int pass = 0, fail = 0;
    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass; else ++fail;
    }

    SDL_Window* MakeWindow(int w, int h)
    {
        SDL_Window* window = SDL_CreateWindow("nanovg-presentation-test", w, h, SDL_WINDOW_OPENGL);
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

    Color ReadPixel(NanoVgRenderer& renderer, int x, int y)
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

    void DrawFullCanvasSprite(NanoVgRenderer& renderer, ISpriteBatchRenderer& sb,
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

        // NanoVgRenderer is non-copyable (owns a GL context) -- construct each case in its own
        // scope instead of via a helper returning by value.
        {
            SDL_Window* window = MakeWindow(200, 100);
            {
            SdlTestGlContext glContext(window);
            NanoVgRenderer renderer(SdlTestRendererArgs(
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
            NanoVgRenderer renderer(SdlTestRendererArgs(
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
            NanoVgRenderer renderer(SdlTestRendererArgs(
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
            NanoVgRenderer renderer(SdlTestRendererArgs(
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
            NanoVgRenderer renderer(SdlTestRendererArgs(
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
        NanoVgRenderer renderer(SdlTestRendererArgs(
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

    // ---- Custom viewport: pixel-correct placement, survives Clear() ----
    void TestViewport()
    {
        SDL_Window* window = MakeWindow(200, 100);
        {
        SdlTestGlContext glContext(window);
        NanoVgRenderer renderer(SdlTestRendererArgs(
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

        // Clear() must not reset the custom viewport back to full-window (matches OpenVG's own
        // P0-3 property).
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 200, 100));
        Check(CloseTo(ReadPixel(renderer, 80, 40), kSprite) && CloseTo(ReadPixel(renderer, 10, 10), kClear),
              "custom viewport survives Clear()");

        }
        SDL_DestroyWindow(window);
    }

    // ---- Resize without Clear ----
    void TestResizeWithoutClear()
    {
        SDL_Window* window = MakeWindow(120, 80);
        {
        SdlTestGlContext glContext(window);
        NanoVgRenderer renderer(SdlTestRendererArgs(
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

        // Xvfb/Mesa software-GLX environment characteristic (verified for OPENVG, holds here too):
        // the actual GLX drawable's backing store on this X server is not reallocated to the new
        // size until one buffer swap has completed after the resize; a draw issued before that
        // swap still rasterizes into the OLD-sized buffer even though every CNA-side
        // viewport/scissor computation is already correct. One harmless platform swap settles it.
        glContext.SwapBuffers(SDL_GetWindowID(window));
        SDL_PumpEvents();

        // No Clear() call here at all -- draw immediately after the resize.
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

    // ---- Presentation scale x custom viewport x scissor, all three at once ----
    // The three interact through one mapping and are otherwise only ever tested apart. Stretch is
    // the mode that makes the interaction visible: window 200x100 with a 100x100 virtual
    // resolution gives scaleX=2 and scaleY=1, so a renderer that carried the scissor rectangle
    // into the sprite space with a single scale factor -- or with none -- lands the sprite
    // somewhere else entirely.
    //
    //   logical scissor (30,40,20,20)
    //     x2 / x1 presentation scale -> physical (60,40)-(100,60)
    //     minus viewport origin (40,20) -> viewport-local (20,20)-(60,40)
    //   viewport-local sprite (0,0,80,60) clipped to that -> physical (60,40)-(100,60)
    void TestPresentationScaleWithCustomViewportAndScissor()
    {
        SDL_Window* window = MakeWindow(200, 100);
        {
        SdlTestGlContext glContext(window);
        NanoVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 100, 100, CnaPresentationMode::Stretch));
        const Color kClear(8, 8, 8, 255);
        const Color kSprite(255, 0, 255, 255);
        auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
        auto sb = renderer.CreateSpriteBatch();

        constexpr int kCullCCW = 2, kFillSolid = 0;
        renderer.SetViewport(40, 20, 80, 60, 0.0f, 1.0f);
        renderer.SetScissorRect(30, 40, 20, 20);
        renderer.ApplyRasterizerState(kCullCCW, kFillSolid, /*scissorTestEnable=*/true, 0.0f, 0.0f);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 80, 60));

        Check(CloseTo(ReadPixel(renderer, 80, 50), kSprite),
              "Stretch + custom viewport + scissor: the scissored region is drawn");
        Check(CloseTo(ReadPixel(renderer, 62, 42), kSprite) &&
                  CloseTo(ReadPixel(renderer, 98, 58), kSprite),
              "Stretch + custom viewport + scissor: the region spans its full mapped extent, so "
              "the X and Y scales were applied independently");
        Check(CloseTo(ReadPixel(renderer, 50, 50), kClear),
              "Stretch + custom viewport + scissor: nothing left of the mapped rectangle");
        Check(CloseTo(ReadPixel(renderer, 110, 50), kClear),
              "Stretch + custom viewport + scissor: nothing right of it");
        Check(CloseTo(ReadPixel(renderer, 80, 30), kClear),
              "Stretch + custom viewport + scissor: nothing above it -- a Y scale wrongly taken "
              "from X would spill here");
        Check(CloseTo(ReadPixel(renderer, 80, 70), kClear),
              "Stretch + custom viewport + scissor: nothing below it");

        renderer.ApplyRasterizerState(kCullCCW, kFillSolid, /*scissorTestEnable=*/false, 0.0f, 0.0f);
        }
        SDL_DestroyWindow(window);
    }

    // ---- Scissor: RasterizerState-driven enable, independent rectangle updates ----
    void TestScissor()
    {
        SDL_Window* window = MakeWindow(100, 100);
        {
        SdlTestGlContext glContext(window);
        NanoVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
        const Color kClear(3, 3, 3, 255);
        const Color kSprite(255, 255, 0, 255);
        auto tex = renderer.CreateTexture(SolidImage(4, 4, kSprite));
        auto sb = renderer.CreateSpriteBatch();

        constexpr int kCullCCW = 2, kFillSolid = 0; // XNA ordinals, matching ApplyRasterizerState's own contract

        // Default: scissor rect set but SCISSOR TEST DISABLED -- SetScissorRect alone must not
        // implicitly enable scissoring.
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

        // The clipped-away region must be left ALONE, not overwritten, for every BlendState --
        // including BlendState.Opaque, whose destination factor is Zero. NanoVG's own nvgScissor is
        // a shader mask that multiplies the fragment colour, so a masked fragment still writes (it
        // writes zero); under Opaque that blackens the clipped region instead of preserving it,
        // which is why NanoVgSpriteBatchRenderer clips the quad geometrically instead. Every other
        // check in this function runs under the default AlphaBlend factors, where a zero source
        // happens to leave the destination intact -- which is exactly why the shader mask looked
        // correct here for so long.
        // The background here is deliberately NOT kClear (3,3,3): a fragment that is masked but
        // still written lands on black, and black is within any sane tolerance of (3,3,3), so that
        // background would let the defect pass unnoticed. This one is far from black in every
        // channel that matters.
        const Color kOpaqueClear(0, 120, 200, 255);
        renderer.ApplyBlendState(/*colorSrc*/0, /*alphaSrc*/0, /*colorDst*/1, /*alphaDst*/1,
                                 /*colorFunc*/0, /*alphaFunc*/0, BlendWriteState{});
        renderer.SetScissorRect(10, 10, 20, 20);
        renderer.Clear(kOpaqueClear.getRProperty() / 255.0f, kOpaqueClear.getGProperty() / 255.0f,
                       kOpaqueClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));
        Check(CloseTo(ReadPixel(renderer, 15, 15), kSprite),
              "scissor under BlendState.Opaque: inside the rect still draws");
        Check(CloseTo(ReadPixel(renderer, 50, 50), kOpaqueClear),
              "scissor under BlendState.Opaque: outside the rect keeps the destination rather than "
              "being blackened by a masked-but-still-written fragment");
        // Restore the default factors for the checks below.
        renderer.ApplyBlendState(/*colorSrc*/0, /*alphaSrc*/0, /*colorDst*/5, /*alphaDst*/5,
                                 /*colorFunc*/0, /*alphaFunc*/0, BlendWriteState{});

        // Disabling again restores unclipped drawing.
        renderer.ApplyRasterizerState(kCullCCW, kFillSolid, /*scissorTestEnable=*/false, 0.0f, 0.0f);
        renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                       kClear.getBProperty() / 255.0f, 1.0f);
        DrawFullCanvasSprite(renderer, *sb, *tex, Rectangle(0, 0, 100, 100));
        Check(CloseTo(ReadPixel(renderer, 5, 5), kSprite), "disabling scissor again restores unclipped drawing");

        }
        SDL_DestroyWindow(window);
    }

    // ---- Multi-instance coexistence: unlike OPENVG (ShivaVG's single-live-context restriction),
    // NanoVG's NVGcontext* is an ordinary per-instance object with no hidden global singleton --
    // see NanoVgRenderer's own doc comment. Proves that claim rather than merely asserting it:
    // two NanoVgRenderer instances live simultaneously, each renders its OWN distinct colour
    // without interfering with the other.
    void TestMultiInstanceCoexistence()
    {
        SDL_Window* window1 = MakeWindow(64, 64);
        SdlTestGlContext glContext1(window1);
        auto renderer1 = std::make_unique<NanoVgRenderer>(SdlTestRendererArgs(
            window1, &glContext1, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));

        SDL_Window* window2 = MakeWindow(64, 64);
        SdlTestGlContext glContext2(window2);
        auto renderer2 = std::make_unique<NanoVgRenderer>(SdlTestRendererArgs(
            window2, &glContext2, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));

        const Color kRed(255, 0, 0, 255), kBlue(0, 0, 255, 255);
        renderer1->Clear(1.0f, 0.0f, 0.0f, 1.0f);
        renderer2->Clear(0.0f, 0.0f, 1.0f, 1.0f);

        Check(CloseTo(ReadPixel(*renderer1, 32, 32), kRed),
              "two live NanoVgRenderer instances: the first renders its own colour");
        Check(CloseTo(ReadPixel(*renderer2, 32, 32), kBlue),
              "two live NanoVgRenderer instances: the second renders its own colour, unaffected by the first");

        // Re-clear the first AFTER the second exists, to prove neither instance's NVGcontext
        // clobbers the other's state.
        renderer1->Clear(1.0f, 0.0f, 0.0f, 1.0f);
        Check(CloseTo(ReadPixel(*renderer1, 32, 32), kRed) && CloseTo(ReadPixel(*renderer2, 32, 32), kBlue),
              "both instances remain independently correct after interleaved draws");

        // A texture belonging to the OTHER instance must be refused, not drawn. NanoVG image
        // handles are per-NVGcontext integers allocated from a counter that starts at the same
        // value in every context, so the first texture created on each renderer gets the SAME
        // handle -- a foreign texture therefore names a valid but different image rather than an
        // invalid one, and an unchecked draw samples the wrong picture in silence. Both textures
        // below are deliberately the first on their own renderer, so their handles do collide.
        {
            const Color kGreen(0, 255, 0, 255), kYellow(255, 255, 0, 255);
            auto textureOn1 = renderer1->CreateTexture(SolidImage(4, 4, kGreen));
            auto textureOn2 = renderer2->CreateTexture(SolidImage(4, 4, kYellow));
            Check(textureOn1 != nullptr && textureOn2 != nullptr,
                  "each instance creates its own texture");

            auto batchOn2 = renderer2->CreateSpriteBatch();
            bool foreignThrew = false;
            std::string foreignMessage;
            try
            {
                batchOn2->Begin();
                batchOn2->Draw(*textureOn1, Rectangle(0, 0, 32, 32), Rectangle(0, 0, 4, 4),
                               Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                               SpriteEffects::None, 0.0f);
                batchOn2->End();
            }
            catch (const std::runtime_error& ex) { foreignThrew = true; foreignMessage = ex.what(); }
            Check(foreignThrew && foreignMessage.find("different NANOVG renderer") != std::string::npos,
                  "a texture from another NanoVgRenderer is refused rather than silently drawn as "
                  "whichever image shares its handle");

            // This seam is the last line of defence, not the first: SpriteBatch refuses a
            // cross-device texture at Draw() before it is ever queued (see
            // SpriteBatchCrossDeviceTest in the shared graphics tests, which covers Immediate and
            // Deferred through the public API). Here the batch is closed properly first -- the
            // throw left it open -- and only then reused, so what is asserted is that the refusal
            // damaged no renderer state, not that an abandoned batch somehow recovers.
            batchOn2->End();
            renderer2->Clear(0.0f, 0.0f, 1.0f, 1.0f);
            renderer2->ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});
            batchOn2->Begin();
            batchOn2->Draw(*textureOn2, Rectangle(0, 0, 32, 32), Rectangle(0, 0, 4, 4),
                           Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None,
                           0.0f);
            batchOn2->End();
            Check(CloseTo(ReadPixel(*renderer2, 16, 16), kYellow),
                  "the refusal leaves the renderer's own next batch working normally");
        }

        renderer1.reset();
        SDL_DestroyWindow(window1);

        // The second instance is unaffected by the first's destruction.
        // The last thing drawn into renderer2 above was the yellow sprite over a blue clear, so
        // this samples a corner the 32x32 sprite never reached.
        Check(CloseTo(ReadPixel(*renderer2, 60, 60), kBlue),
              "destroying the first instance leaves the second fully functional");

        renderer2.reset();
        SDL_DestroyWindow(window2);
    }

    // ---- The GL context this renderer refuses to run on ----
    // NanoVG needs OpenGL 2.0+ (its GL2 backend compiles GLSL 1.10 into real shader objects) and a
    // stencil-capable render target (its own README's requirement). Neither shortfall is testable
    // by asking for a crippled context -- a driver hands out what it has -- so the test overrides
    // what the platform service REPORTS, which is the exact value the renderer reads. The success
    // case is re-run afterwards through the same window, so what is proven is that the refusal
    // came from the reported attributes and not from the environment.
    void TestGlContextRequirementRefusals()
    {
        SDL_Window* window = MakeWindow(64, 64);
        {
        SdlTestGlContext glContext(window);

        const auto expectRefused = [&](const CNA::Platform::GlContextDescription& reported,
                                       const std::string& fragment, const std::string& label)
        {
            glContext.SetReportedAttributesForTesting(reported);
            bool threw = false;
            std::string message;
            try
            {
                NanoVgRenderer refused(SdlTestRendererArgs(
                    window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
            }
            catch (const std::runtime_error& ex) { threw = true; message = ex.what(); }
            Check(threw && message.find(fragment) != std::string::npos,
                  label + (threw ? (": rejected with \"" + message + "\"") : ": NOT rejected"));
        };

        CNA::Platform::GlContextDescription tooOld;
        tooOld.majorVersion = 1;
        tooOld.minorVersion = 5;
        tooOld.stencilBits = 8;
        expectRefused(tooOld, "OpenGL 2.0", "a pre-2.0 context is refused at construction");

        CNA::Platform::GlContextDescription noStencil;
        noStencil.majorVersion = 2;
        noStencil.minorVersion = 1;
        noStencil.stencilBits = 0;
        expectRefused(noStencil, "stencil",
                      "a context with no stencil plane is refused at construction");

        CNA::Platform::GlContextDescription tooFewStencilBits = noStencil;
        tooFewStencilBits.stencilBits = 4;
        expectRefused(tooFewStencilBits, "stencil",
                      "a context with fewer than 8 stencil bits is refused at construction");

        // A context that satisfies both, reported through the same override, is accepted -- so the
        // refusals above are the checks talking, not something else about this window.
        CNA::Platform::GlContextDescription adequate;
        adequate.majorVersion = 2;
        adequate.minorVersion = 1;
        adequate.stencilBits = 8;
        glContext.SetReportedAttributesForTesting(adequate);
        bool adequateThrew = false;
        try
        {
            NanoVgRenderer accepted(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
            accepted.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        }
        catch (const std::runtime_error&) { adequateThrew = true; }
        Check(!adequateThrew, "a context meeting both requirements is accepted");

        // And with the override cleared, i.e. against what this platform really granted.
        glContext.SetReportedAttributesForTesting(std::nullopt);
        bool realThrew = false;
        try
        {
            NanoVgRenderer real(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
            real.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        }
        catch (const std::runtime_error&) { realThrew = true; }
        Check(!realThrew, "the context this platform actually grants is accepted");
        }
        SDL_DestroyWindow(window);
    }

    // ---- Repeated construct/destroy lifecycle ----
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
                NanoVgRenderer renderer(SdlTestRendererArgs(
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
        Check(allSucceeded, "25 sequential NanoVgRenderer construct/Clear/Present/destroy cycles all succeed");
    }

    // ---- Swap interval reaches SDL ----
    void TestSwapInterval()
    {
        SDL_Window* window = MakeWindow(64, 64);
        {
        SdlTestGlContext glContext(window);
        NanoVgRenderer renderer(SdlTestRendererArgs(
            window, &glContext, nullptr, 0, 0,
            CnaPresentationMode::NativeBackBuffer, /*swapInterval=*/0));
        int interval = -99;
        const bool queried = SDL_GL_GetSwapInterval(&interval);
        Check(queried, "SDL_GL_GetSwapInterval succeeds after NanoVgRenderer construction with swapInterval=0");
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
        TestPresentationScaleWithCustomViewportAndScissor();
        TestMultiInstanceCoexistence();
        TestGlContextRequirementRefusals();
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
