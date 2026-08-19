// SPDX-License-Identifier: MS-PL
// Adversarial proof that NANOVG honours SpriteSortMode::Immediate as an ORDERING guarantee, not
// just as a label.
//
// NanoVG normally submits nothing until nvgEndFrame(), which calls the backend's own
// renderFlush(). A SpriteBatch built on that defers every Draw() to End() -- which is correct for
// SpriteSortMode::Deferred and wrong for Immediate, where ISpriteBatchRenderer::SetImmediateMode's
// own contract requires the renderer to flush per draw so that a GraphicsDevice operation issued
// BETWEEN two Draw() calls is honestly ordered against the sprites around it.
//
// The decisive case needs no sprite-vs-sprite comparison at all -- a Clear() between the Draw()
// and the End() separates the two modes completely:
//
//     Begin(mode); Draw(red); Clear(green); End();
//
//   * Immediate: the red sprite is already on the surface when Draw() returns, so the Clear wipes
//     it and the pixel is GREEN.
//   * Deferred:  the sprite is still queued when the Clear runs, so it lands on top afterwards and
//     the pixel is RED.
//
// Both expectations are asserted, in the same run, against the same geometry: a renderer that
// ignores the immediate flag produces RED for both, and a renderer that flushed unconditionally
// would produce GREEN for both. Only a renderer that actually distinguishes the two modes passes.
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
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::NanoVg;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;

namespace
{
    int pass = 0, fail = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass; else ++fail;
    }

    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    /// Every colour here is a pure primary written under BlendState.Opaque, so only 8-bit
    /// round-trip noise separates a correct read from its expected value.
    constexpr int kTolerance = 4;

    ImageData Solid(int w, int h, const Color& c)
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

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + ")";
    }

    bool CloseTo(const Color& a, const Color& b)
    {
        const auto d = [](int x, int y) { return x > y ? x - y : y - x; };
        return d(a.getRProperty(), b.getRProperty()) <= kTolerance &&
               d(a.getGProperty(), b.getGProperty()) <= kTolerance &&
               d(a.getBProperty(), b.getBProperty()) <= kTolerance;
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
        SDL_Window* window = SDL_CreateWindow("nanovg-immediate-mode", 160, 120, SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            NanoVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
            // Opaque, so a sprite that reaches the surface fully replaces what was there and the
            // readback is unambiguous about which of the two landed last.
            renderer.ApplyBlendState(/*colorSrc*/0, /*alphaSrc*/0, /*colorDst*/1, /*alphaDst*/1,
                                     /*colorFunc*/0, /*alphaFunc*/0, BlendWriteState{});

            const auto clearTo = [&renderer](const Color& c)
            {
                renderer.Clear(c.getRProperty() / 255.0f, c.getGProperty() / 255.0f,
                               c.getBProperty() / 255.0f, 1.0f);
            };
            const auto readPixel = [&renderer](int x, int y)
            {
                uint8_t rgba[4] = {0, 0, 0, 0};
                renderer.ReadBackbuffer(x, y, 1, 1, rgba);
                return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
            };

            auto red = renderer.CreateTexture(Solid(4, 4, kRed));
            auto blue = renderer.CreateTexture(Solid(4, 4, kBlue));
            auto sprites = renderer.CreateSpriteBatch();

            const auto drawAt = [&](const std::unique_ptr<ITextureRenderer>& texture, int x, int y)
            {
                sprites->Draw(*texture, Rectangle(x, y, 40, 40), Rectangle(0, 0, 4, 4),
                              Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
            };

            // ---- The decisive pair: a Clear between the Draw and the End ---------------------
            sprites->SetImmediateMode(true);
            clearTo(kBlue);
            sprites->Begin();
            drawAt(red, 10, 10);
            clearTo(kGreen);
            sprites->End();
            const Color immediateResult = readPixel(30, 30);
            Check(CloseTo(immediateResult, kGreen),
                  "Immediate: a sprite drawn before a Clear is already on the surface, so the "
                  "Clear wipes it -- expected " + Describe(kGreen) + ", got " +
                      Describe(immediateResult));

            sprites->SetImmediateMode(false);
            clearTo(kBlue);
            sprites->Begin();
            drawAt(red, 10, 10);
            clearTo(kGreen);
            sprites->End();
            const Color deferredResult = readPixel(30, 30);
            Check(CloseTo(deferredResult, kRed),
                  "Deferred: the same sprite is still queued when the Clear runs, so it lands on "
                  "top afterwards -- expected " + Describe(kRed) + ", got " +
                      Describe(deferredResult));

            Check(!CloseTo(immediateResult, deferredResult),
                  "Immediate and Deferred produce observably different results for the identical "
                  "call sequence");

            // ---- Ordering BETWEEN two Immediate draws ---------------------------------------
            // The first sprite must be wiped by the intervening Clear while the second survives:
            // a renderer that flushes only at End() would show both, because both would land after
            // the Clear.
            sprites->SetImmediateMode(true);
            clearTo(kBlue);
            sprites->Begin();
            drawAt(red, 10, 10);
            clearTo(kGreen);
            drawAt(blue, 60, 10);
            sprites->End();
            Check(CloseTo(readPixel(30, 30), kGreen),
                  "Immediate: the sprite drawn BEFORE the intervening Clear is gone -- got " +
                      Describe(readPixel(30, 30)));
            Check(CloseTo(readPixel(80, 30), kBlue),
                  "Immediate: the sprite drawn AFTER the intervening Clear survives -- got " +
                      Describe(readPixel(80, 30)));

            // ---- Flushing per draw must not damage the batch's own state ---------------------
            // renderFlush empties the recorded call list but leaves the frame open, so the
            // scissor, transform and blend factors established at Begin() have to survive it.
            // Deliberately NOT nvgEndFrame()/nvgBeginFrame(), which would run nvgReset().
            renderer.SetScissorRect(0, 0, 60, 120);
            renderer.ApplyRasterizerState(/*cullMode*/0, /*fillMode*/0, /*scissorTestEnable*/true,
                                          0.0f, 0.0f);
            sprites->SetImmediateMode(true);
            clearTo(kGreen);
            sprites->Begin();
            drawAt(red, 10, 10);   // inside the scissor
            drawAt(blue, 70, 10);  // outside it
            sprites->End();
            Check(CloseTo(readPixel(30, 30), kRed),
                  "Immediate: the batch's scissor still admits what it should after a per-draw "
                  "flush -- got " + Describe(readPixel(30, 30)));
            Check(CloseTo(readPixel(90, 30), kGreen),
                  "Immediate: the batch's scissor still rejects what it should on the SECOND draw, "
                  "i.e. the flush did not reset the frame state -- got " +
                      Describe(readPixel(90, 30)));
            renderer.ApplyRasterizerState(0, 0, /*scissorTestEnable*/false, 0.0f, 0.0f);

            // ---- Immediate batches still composite normally when nothing intervenes ----------
            sprites->SetImmediateMode(true);
            clearTo(kGreen);
            sprites->Begin();
            drawAt(red, 10, 10);
            drawAt(blue, 60, 10);
            sprites->End();
            Check(CloseTo(readPixel(30, 30), kRed) && CloseTo(readPixel(80, 30), kBlue),
                  "Immediate: several sprites in one batch all reach the surface");

            // ---- Device state changed BETWEEN two Immediate draws ---------------------------
            // The ordering fix alone does not deliver this: a per-draw flush only submits work that
            // was already RECORDED against the state captured at Begin(). SetImmediateMode's own
            // contract is explicitly about state changed between two Draw() calls, so the state has
            // to be re-read per draw as well. Deferred deliberately keeps the batch snapshot -- all
            // of its Draw() calls run from End() under one device state anyway -- so only Immediate
            // is asserted here.
            {
                // Straight red at half alpha over an opaque green background: Opaque writes it
                // un-attenuated as (255,0,0), NonPremultiplied composites it to about (128,127,0).
                // Nothing subtle separates the two.
                auto translucentRed = renderer.CreateTexture(Solid(4, 4, Color(255, 0, 0, 128)));
                const auto applyOpaque = [&renderer]
                {
                    renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});
                };
                const auto applyNonPremultiplied = [&renderer]
                {
                    renderer.ApplyBlendState(4, 4, 5, 5, 0, 0, BlendWriteState{});
                };

                applyOpaque();
                clearTo(kGreen);
                sprites->SetImmediateMode(true);
                sprites->Begin();
                drawAt(translucentRed, 10, 10);
                applyNonPremultiplied();
                drawAt(translucentRed, 60, 10);
                sprites->End();

                const Color firstSprite = readPixel(30, 30);
                const Color secondSprite = readPixel(80, 30);
                Check(CloseTo(firstSprite, kRed),
                      "Immediate: the sprite drawn BEFORE the BlendState change keeps the old state "
                      "-- expected " + Describe(kRed) + ", got " + Describe(firstSprite));
                Check(CloseTo(secondSprite, Color(128, 127, 0, 255)),
                      "Immediate: the sprite drawn AFTER the BlendState change uses the NEW state "
                      "-- expected (128,127,0), got " + Describe(secondSprite));

                applyOpaque();
            }

            // ---- Viewport changed BETWEEN two Immediate draws --------------------------------
            // The sprite coordinate space follows GraphicsDevice.Viewport, so a viewport set
            // between two Immediate draws has to re-open the frame at the new extent -- otherwise
            // the second sprite is projected through the previous space and squashed into the new
            // rasterizer viewport.
            {
                clearTo(kGreen);
                sprites->SetImmediateMode(true);
                sprites->Begin();
                drawAt(red, 10, 10);
                renderer.SetViewport(80, 0, 40, 30, 0.0f, 1.0f);
                // Viewport-local: the full viewport rectangle, which must land exactly on the
                // physical rectangle (80,0,40,30) rather than a shrunken corner of it.
                sprites->Draw(*blue, Rectangle(0, 0, 40, 30), Rectangle(0, 0, 4, 4), Color::White,
                              0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
                renderer.SetViewport(0, 0, 160, 120, 0.0f, 1.0f);

                Check(CloseTo(readPixel(30, 30), kRed),
                      "Immediate: the sprite drawn before the Viewport change is unaffected");
                Check(CloseTo(readPixel(82, 2), kBlue) && CloseTo(readPixel(117, 27), kBlue),
                      "Immediate: the sprite drawn after the Viewport change fills the NEW viewport "
                      "corner to corner -- got " + Describe(readPixel(82, 2)) + " and " +
                          Describe(readPixel(117, 27)));
                Check(CloseTo(readPixel(122, 15), kGreen),
                      "Immediate: nothing is drawn outside the new viewport -- got " +
                          Describe(readPixel(122, 15)));
            }

            // ---- The flag is per batch, not sticky ------------------------------------------
            sprites->SetImmediateMode(false);
            clearTo(kBlue);
            sprites->Begin();
            drawAt(red, 10, 10);
            clearTo(kGreen);
            sprites->End();
            Check(CloseTo(readPixel(30, 30), kRed),
                  "a Deferred batch following an Immediate one is deferred again");
        }
        SDL_DestroyWindow(window);
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
