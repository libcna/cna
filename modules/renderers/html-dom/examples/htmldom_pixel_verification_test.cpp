// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md Phase D9: pixel-exact browser verification for everything Phase D1-D8 left
// checked only structurally ("did something draw", "did the CSS property appear") rather than
// against an actual hand-derived expected value. Distinct from htmldom_smoke_test.cpp, which
// covers the DOM-surface/pool/RenderTarget2D-readback/backbuffer-refusal contract.
//
// Every check here follows the same pattern: draw into a RenderTarget2D under a specific,
// deliberately chosen BlendState/transform, unbind, read the target back with
// RenderTarget2D::GetData, and compare against a value derived by hand from the documented
// per-pixel formula -- not just "did it throw" or "did an element appear".
//
// Driven by the same scripts/run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as
// the smoke test: pass this page's path as the argument.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "System/IO/MemoryStream.hpp"

#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kExpectedChecks = 36;

#if defined(__EMSCRIPTEN__)
    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    void JsPublishResult(int, int, int) {}
#endif

    Texture2D Make1x1(GraphicsDevice& dev, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        return Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{r, g, b, a});
    }

    // Channel-wise tolerance for a pixel comparison. Real algebra bugs (wrong operand, double
    // application of a division, a swapped factor) produce errors far larger than this; the
    // tolerance exists only to absorb legitimate 8-bit rounding -- this renderer's own
    // round-to-nearest during its per-pixel maths, PLUS (for the AlphaBlend check specifically)
    // the browser's own internal premultiply-for-compositing/un-premultiply-for-readback round
    // trip, which is a second, independent source of +-1-ish rounding this renderer does not
    // control at all.
    bool CloseEnough(std::uint8_t actual, int expected, int tolerance)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }
}

class HtmlDomPixelVerificationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<RenderTarget2D> fontRt_;
    std::unique_ptr<Texture2D> fontAtlas_;
    std::unique_ptr<SpriteFont> font_;
    std::unique_ptr<RenderTarget2D> rtA_;
    std::unique_ptr<RenderTarget2D> rtB_;
    std::unique_ptr<RenderTarget2D> tileRt_;
    std::unique_ptr<Texture2D> tileTex_;
    std::unique_ptr<RenderTarget2D> atlasRt_;
    std::unique_ptr<Texture2D> atlasTexture_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
    }

    // Clears rt_ to fully transparent, runs `draw`, unbinds, and returns pixel (0,0). `draw` runs
    // with a real SpriteBatch Begin/End session already active around it via the supplied
    // sortMode/blendState -- callers just issue Draw() calls.
    template <typename DrawFn>
    Color ReadBackTopLeftPixel(SpriteSortMode sortMode, BlendState blendState, DrawFn draw)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 0));
        spriteBatch_->Begin(sortMode, blendState, nullptr, nullptr, nullptr);
        draw();
        spriteBatch_->End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            rt_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        }
        catch (const std::exception& e)
        {
            std::printf("       GetData threw: %s\n", e.what());
        }
        return pixels[0];
    }

    // Clears `target` to fully transparent, runs `draw`, unbinds, and returns the whole target's
    // pixels as a flat row-major (top row first) array -- for tests that need to sample more than
    // one location (glyph placement, scale extent).
    template <typename DrawFn>
    std::vector<Color> ReadBackWholeTarget(RenderTarget2D& target, int w, int h, DrawFn draw)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 0));
        draw();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        }
        catch (const std::exception& e)
        {
            std::printf("       GetData threw: %s\n", e.what());
        }
        return pixels;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        rt_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 2, 2);
        fontRt_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 20, 20);
        rtA_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 4, 4);
        rtB_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 20, 20);
        tileRt_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 4, 4);

        // plan_html_dom.md HTMLDOM-97: 2x2 source, same colour layout the smoke test's own Wrap-tile
        // check (frame 6) uses -- (0,0)=red (1,0)=green (0,1)=blue (1,1)=yellow -- so Mirror's
        // pixel-exact expectation can be derived and cross-checked against Wrap's already-verified
        // one by hand, rather than invented fresh.
        tileTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 2, std::vector<std::uint8_t>{
                255, 0, 0, 255,   0, 255, 0, 255,
                0, 0, 255, 255,   255, 255, 0, 255,
            }));

        // plan_html_dom.md HTMLDOM-88: an 8x4 atlas holding two 4x4 glyphs. 'A' is split
        // left-half-red/right-half-blue so a horizontal flip is visually unambiguous (the two
        // halves swap); 'B' is solid green so it's trivially distinguishable from 'A' at a
        // glance. Distinct, nonzero kerning on 'A' (rightBearing=2) makes same-line advance a
        // real, checkable quantity instead of an accidental default.
        std::vector<std::uint8_t> atlas(8 * 4 * 4, 0);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                const std::size_t iLeft = (static_cast<std::size_t>(y) * 8 + x) * 4;
                atlas[iLeft] = 255; atlas[iLeft + 1] = 0; atlas[iLeft + 2] = 0; atlas[iLeft + 3] = 255;
                const std::size_t iRight = (static_cast<std::size_t>(y) * 8 + x + 2) * 4;
                atlas[iRight] = 0; atlas[iRight + 1] = 0; atlas[iRight + 2] = 255; atlas[iRight + 3] = 255;
            }
            for (int x = 4; x < 8; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 8 + x) * 4;
                atlas[i] = 0; atlas[i + 1] = 255; atlas[i + 2] = 0; atlas[i + 3] = 255;
            }
        }
        fontAtlas_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 8, 4, atlas));

        // plan_html_dom.md HTMLDOM-119: a 4x4 "atlas" with a hard, unpadded edge between two
        // regions -- left half (columns 0-1) solid red, right half (columns 2-3) solid blue -- the
        // same shape a real, unpadded sprite atlas has between two adjacent packed sprites. Drawing
        // ONLY the red half (an IN-BOUNDS source rect, distinct from HTMLDOM-104's already-fixed
        // OUT-of-bounds Clamp overflow) scaled up under linear filtering is what can reveal GPU
        // bilinear sampling reading past its own source rect into the adjacent blue region.
        atlasRt_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 44, 44);
        std::vector<std::uint8_t> atlasPixels(4 * 4 * 4, 0);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 4 + x) * 4;
                if (x < 2) { atlasPixels[i] = 255; atlasPixels[i+1] = 0; atlasPixels[i+2] = 0; atlasPixels[i+3] = 255; }
                else       { atlasPixels[i] = 0; atlasPixels[i+1] = 0; atlasPixels[i+2] = 255; atlasPixels[i+3] = 255; }
            }
        }
        atlasTexture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 4, 4, atlasPixels));
        font_ = std::make_unique<SpriteFont>(
            *fontAtlas_,
            std::vector<Rectangle>{Rectangle(0, 0, 4, 4), Rectangle(4, 0, 4, 4)},
            std::vector<Rectangle>{Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4)},
            std::vector<charcs>{u'A', u'B'},
            /*lineSpacing=*/8, /*spacing=*/0.0f,
            std::vector<Vector3>{Vector3(0.0f, 4.0f, 2.0f), Vector3(0.0f, 4.0f, 0.0f)},
            std::nullopt);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black);

        // plan_html_dom.md HTMLDOM-84: tint is RGB * colour / 255, alpha untouched. An opaque
        // (255-2, 150, 100, 255) texel over a transparent-black target under AlphaBlend, tinted
        // (255, 128, 64, 255): since the destination starts fully transparent, source-over
        // compositing reduces to exactly the tinted colour, so this isolates the tint maths from
        // any compositing-order effects.
        if (frame_ == 1)
        {
            Texture2D tex = Make1x1(dev, 200, 150, 100, 255);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::AlphaBlend, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 128, 64, 255));
            });
            // Expected via the same float-multiply-divide-by-255 formula CNA_HtmlDom_InstallTextureHelpers
            // uses, so this is a genuine cross-check of the JS arithmetic, not a restatement of it.
            const int wantR = static_cast<int>(200.0 * 255 / 255 + 0.5);
            const int wantG = static_cast<int>(150.0 * 128 / 255 + 0.5);
            const int wantB = static_cast<int>(100.0 * 64 / 255 + 0.5);
            std::printf("       tint got (%d,%d,%d) want ~(%d,%d,%d)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        wantR, wantG, wantB);
            check(CloseEnough(result.getRProperty(), wantR, 1) &&
                  CloseEnough(result.getGProperty(), wantG, 1) &&
                  CloseEnough(result.getBProperty(), wantB, 1),
                  "HTMLDOM-84: tint multiplies RGB by the draw colour exactly, alpha untouched");
        }

        // plan_html_dom.md HTMLDOM-85 (highest-risk item): AlphaBlend's contract assumes ALREADY
        // premultiplied source data (srcBlend=One). Upload a texel that is deliberately
        // premultiplied by hand -- straight colour (255,100,50) at alpha=128 becomes
        // (255*128/255, 100*128/255, 50*128/255, 128) = (128, 50, 25, 128), mirroring
        // SDL_RENDERER's own Task 697 pixel test -- and confirm the renderer's un-premultiply pass
        // reconstructs the ORIGINAL straight colour, not a double-divided or undivided one.
        if (frame_ == 2)
        {
            Texture2D tex = Make1x1(dev, 128, 50, 25, 128);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::AlphaBlend, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 255, 255, 255));
            });
            std::printf("       alphablend got (%d,%d,%d,%d) want ~(255,100,50,128)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            // Wider tolerance: this result passes through TWO independent 8-bit roundings -- this
            // renderer's own un-premultiply division, then the browser's own internal
            // premultiply-for-compositing/un-premultiply-for-readback round trip.
            check(CloseEnough(result.getRProperty(), 255, 2) &&
                  CloseEnough(result.getGProperty(), 100, 2) &&
                  CloseEnough(result.getBProperty(), 50, 2) &&
                  CloseEnough(result.getAProperty(), 128, 2),
                  "HTMLDOM-85: AlphaBlend un-premultiplies premultiplied source data correctly");

            // HTMLDOM-124: the draw colour is premultiplied under AlphaBlend too. The DOM path
            // applies RGB to straight-alpha pixels and A separately as opacity/globalAlpha, so it
            // must recover straight tint RGB first. An opaque source makes the regression exact:
            // before the fix this alpha-only white fade read back half-darkened RGB (~100,50,25).
            Texture2D opaqueTex = Make1x1(dev, 200, 100, 50, 255);
            const Color faded = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::AlphaBlend, [&] {
                spriteBatch_->Draw(opaqueTex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color::FromNonPremultiplied(255, 255, 255, 128));
            });
            std::printf("       alphablend tint fade got (%d,%d,%d,%d) want ~(200,100,50,128)\n",
                        faded.getRProperty(), faded.getGProperty(), faded.getBProperty(),
                        faded.getAProperty());
            check(CloseEnough(faded.getRProperty(), 200, 2) &&
                  CloseEnough(faded.getGProperty(), 100, 2) &&
                  CloseEnough(faded.getBProperty(), 50, 2) &&
                  CloseEnough(faded.getAProperty(), 128, 2),
                  "HTMLDOM-124: AlphaBlend alpha-only tint changes opacity without double-darkening RGB");
        }

        // plan_html_dom.md HTMLDOM-86/HTMLDOM-100: Opaque replaces the destination with the source
        // pixel exactly, alpha included -- confirmed via this project's own BlendState.cpp, which
        // defines BlendState::Opaque with symmetric One/Zero factors for BOTH colour and alpha, not
        // just colour. This test binds a RenderTarget2D, so it exercises the Canvas2D path, which
        // reproduces that with 'copy' compositing on the un-modified source pixels. Use a GENUINELY
        // semi-transparent source texel (every prior check used alpha=255 source data, so a strip-to-
        // 255 bug would have been a no-op every time it ran) -- if RGB came back darkened, 'copy'
        // would have blended with the cleared-transparent background instead of replacing it
        // outright; if alpha came back as 255 rather than the source's own 120, the destination
        // would not have been genuinely replaced.
        if (frame_ == 3)
        {
            Texture2D tex = Make1x1(dev, 180, 90, 40, 120);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::Opaque, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 255, 255, 255));
            });
            std::printf("       opaque got (%d,%d,%d,%d) want ~(180,90,40,120)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            check(CloseEnough(result.getRProperty(), 180, 1) &&
                  CloseEnough(result.getGProperty(), 90, 1) &&
                  CloseEnough(result.getBProperty(), 40, 1) &&
                  CloseEnough(result.getAProperty(), 120, 1),
                  "HTMLDOM-86: Opaque replaces the destination with the source pixel including its "
                  "own alpha, not forced to 255, on the Canvas2D render-target path");
        }

        // plan_html_dom.md HTMLDOM-87: Additive must actually ADD channel values (clamped), not
        // just apply SOME blend. Two fully opaque, non-overlapping-in-colour-space draws
        // (pure red then pure blue) at the exact same destination pixel: a correct 'lighter'
        // composite must read back as pure magenta at full alpha (255+0, 0, 0+255 clamped); a
        // renderer that fell back to plain source-over would read back as just blue (the second
        // draw fully overwriting the first).
        if (frame_ == 4)
        {
            Texture2D red = Make1x1(dev, 255, 0, 0, 255);
            Texture2D blue = Make1x1(dev, 0, 0, 255, 255);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::Additive, [&] {
                spriteBatch_->Draw(red, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->Draw(blue, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            });
            std::printf("       additive got (%d,%d,%d,%d) want (255,0,255,255)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            check(result.getRProperty() == 255 && result.getGProperty() == 0 &&
                  result.getBProperty() == 255 && result.getAProperty() == 255,
                  "HTMLDOM-87: Additive sums overlapping opaque colours exactly (clamped), not "
                  "a plain overwrite");
        }

        // plan_html_dom.md HTMLDOM-88: DrawString's newline handling -- never tested before. 'A'
        // (left-half red, right-half blue) then '\n' then 'B' (green), lineSpacing=8, at
        // position (4,4). Both glyphs have cropping (0,0) and 'A' is first-in-line on both lines
        // (kern.X's abs() applies the same way), so hand-derived from SpriteBatch::DrawString's
        // own formula (see plan_html_dom.md's task note): A lands at exactly (4,4), B lands at
        // exactly (4, 4+8)=(4,12) -- proving \n resets X back to the left margin and advances Y
        // by lineSpacing, not smearing B onto the same line as A.
        if (frame_ == 5)
        {
            const auto pixels = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin();
                spriteBatch_->DrawString(*font_, "A\nB", Vector2(4, 4), Color::White);
                spriteBatch_->End();
            });
            const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 20 + x]; };
            const Color aLeft = at(5, 5), aRight = at(7, 5), bOnLine2 = at(5, 13);
            std::printf("       newline: A-left(5,5)=(%d,%d,%d) A-right(7,5)=(%d,%d,%d) "
                        "B-line2(5,13)=(%d,%d,%d)\n",
                        aLeft.getRProperty(), aLeft.getGProperty(), aLeft.getBProperty(),
                        aRight.getRProperty(), aRight.getGProperty(), aRight.getBProperty(),
                        bOnLine2.getRProperty(), bOnLine2.getGProperty(), bOnLine2.getBProperty());
            check(aLeft.getRProperty() > 200 && aLeft.getBProperty() < 50 &&
                  aRight.getBProperty() > 200 && aRight.getRProperty() < 50 &&
                  bOnLine2.getGProperty() > 200 && bOnLine2.getRProperty() < 50,
                  "HTMLDOM-88a: DrawString's \\n resets X to the left margin and advances Y by "
                  "lineSpacing, rather than smearing the next glyph onto the same line");
        }

        // plan_html_dom.md HTMLDOM-88: same-line multi-glyph kerning advance -- "AB" with 'A's
        // kerning triple (0, width=4, rightBearing=2). Hand-derived: B's left edge lands at
        // exactly position.X + glyphWidth(4) + rightBearing(2) = position.X + 6, proving the
        // kerning-driven advance between two DIFFERENT glyphs in one DrawString call reaches this
        // renderer's Draw() correctly (not silently zeroed or only applied to a single-glyph case).
        if (frame_ == 6)
        {
            const auto pixels = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin();
                spriteBatch_->DrawString(*font_, "AB", Vector2(4, 4), Color::White);
                spriteBatch_->End();
            });
            const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 20 + x]; };
            const Color aLeft = at(5, 5), justBeforeB = at(9, 5), bGlyph = at(11, 5);
            std::printf("       kerning: A-left(5,5)=(%d,%d,%d) gap(9,5)=(%d,%d,%d,%d) "
                        "B(11,5)=(%d,%d,%d)\n",
                        aLeft.getRProperty(), aLeft.getGProperty(), aLeft.getBProperty(),
                        justBeforeB.getRProperty(), justBeforeB.getGProperty(),
                        justBeforeB.getBProperty(), justBeforeB.getAProperty(),
                        bGlyph.getRProperty(), bGlyph.getGProperty(), bGlyph.getBProperty());
            check(aLeft.getRProperty() > 200 && aLeft.getBProperty() < 50 &&
                  justBeforeB.getAProperty() < 50 &&
                  bGlyph.getGProperty() > 200 && bGlyph.getRProperty() < 50,
                  "HTMLDOM-88b: same-line kerning advance (glyphWidth+rightBearing) places the "
                  "next glyph exactly where SpriteFont's kerning triple says, with a real gap "
                  "in between");
        }

        // plan_html_dom.md HTMLDOM-88: DrawString's rotation/scale/flip overload -- distinct code
        // from the 4-arg overload used everywhere else in this project's HTML_DOM tests. scale=2
        // on the 4x4 'B' glyph must produce an 8x8 rendered glyph, not a 4x4 one: (10,10) is
        // inside the scaled box but outside the unscaled one, so it alone distinguishes "scale
        // forwarded correctly" from "scale silently ignored".
        if (frame_ == 7)
        {
            const auto pixels = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin();
                spriteBatch_->DrawString(*font_, "B", Vector2(4, 4), Color::White, 0.0f,
                                         Vector2::Zero, 2.0f, SpriteEffects::None, 0.0f);
                spriteBatch_->End();
            });
            const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 20 + x]; };
            const Color insideScaled = at(10, 10), outsideScaled = at(13, 13);
            std::printf("       scale: inside(10,10)=(%d,%d,%d,%d) outside(13,13)=(%d,%d,%d,%d)\n",
                        insideScaled.getRProperty(), insideScaled.getGProperty(),
                        insideScaled.getBProperty(), insideScaled.getAProperty(),
                        outsideScaled.getRProperty(), outsideScaled.getGProperty(),
                        outsideScaled.getBProperty(), outsideScaled.getAProperty());
            check(insideScaled.getGProperty() > 200 && insideScaled.getRProperty() < 50 &&
                  outsideScaled.getAProperty() < 50,
                  "HTMLDOM-88c: DrawString's rotation/scale/flip overload forwards scale "
                  "correctly (4x4 glyph renders as 8x8, not silently ignored)");
        }

        // plan_html_dom.md HTMLDOM-88: SpriteEffects::FlipHorizontally via the same overload.
        // DrawString's own flip handling shifts the WHOLE string's anchor by MeasureString(text).X
        // in addition to the per-glyph mirror Draw() already applies (SpriteBatch::DrawString:
        // "effects != None" branch) -- so, unlike Draw()'s flip (already unit-tested to keep the
        // destination footprint fixed and mirror only the content), the glyph's absolute position
        // is not safe to hand-predict here without re-deriving that shift by hand. Instead this
        // checks the OBSERVABLE INVARIANT that must hold regardless of where the shifted glyph
        // lands: red is left-of-blue when unflipped, and right-of-blue when flipped. Scans the
        // whole target for the two colours' average X rather than sampling fixed coordinates, so
        // it is robust to the exact position DrawString's flip shift produces.
        if (frame_ == 8)
        {
            const auto scanAverageX = [](const std::vector<Color>& pixels, int w, int h,
                                         bool wantRed) -> double {
                double sumX = 0.0; int n = 0;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const Color& c = pixels[static_cast<std::size_t>(y) * w + x];
                        const bool isRed = c.getRProperty() > 200 && c.getBProperty() < 50;
                        const bool isBlue = c.getBProperty() > 200 && c.getRProperty() < 50;
                        if ((wantRed && isRed) || (!wantRed && isBlue)) { sumX += x; ++n; }
                    }
                return n > 0 ? sumX / n : -1.0;
            };

            const auto unflipped = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin();
                spriteBatch_->DrawString(*font_, "A", Vector2(4, 4), Color::White, 0.0f,
                                         Vector2::Zero, 1.0f, SpriteEffects::None, 0.0f);
                spriteBatch_->End();
            });
            const double redXUnflipped = scanAverageX(unflipped, 20, 20, true);
            const double blueXUnflipped = scanAverageX(unflipped, 20, 20, false);

            const auto flipped = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin();
                spriteBatch_->DrawString(*font_, "A", Vector2(4, 4), Color::White, 0.0f,
                                         Vector2::Zero, 1.0f, SpriteEffects::FlipHorizontally, 0.0f);
                spriteBatch_->End();
            });
            const double redXFlipped = scanAverageX(flipped, 20, 20, true);
            const double blueXFlipped = scanAverageX(flipped, 20, 20, false);

            std::printf("       flip: unflipped red.x=%.1f blue.x=%.1f | flipped red.x=%.1f blue.x=%.1f\n",
                        redXUnflipped, blueXUnflipped, redXFlipped, blueXFlipped);
            check(redXUnflipped >= 0 && blueXUnflipped >= 0 && redXFlipped >= 0 && blueXFlipped >= 0 &&
                  redXUnflipped < blueXUnflipped && redXFlipped > blueXFlipped,
                  "HTMLDOM-88d: DrawString's rotation/scale/flip overload forwards "
                  "SpriteEffects::FlipHorizontally correctly (red/blue relative order reverses)");
        }

        // plan_html_dom.md HTMLDOM-82: Begin(transformMatrix=...) has real code (design decision
        // 5's matrix -> CSS matrix()/ctx.transform() mapping) but zero numeric verification --
        // the existing GTest only checks it doesn't throw, and the smoke test only checks the CSS
        // matrix(...) string is present, never that a NON-identity matrix actually moves a sprite.
        // A pure translation is deliberately chosen over a rotation: it exercises the matrix's
        // M41/M42 -> CSS e/f mapping unambiguously, with no sign-convention risk the way a
        // rotation matrix would carry (getting XNA's own rotation-matrix layout wrong by hand
        // would produce a wrong TEST, not a wrong implementation -- not a risk worth taking here).
        // Drawn into a bound render target, so this also exercises transformMatrix on the
        // Canvas2D `targetCtx` branch specifically, a separate code path from the DOM branch the
        // smoke test's CSS-string check covers.
        if (frame_ == 9)
        {
            Texture2D tex = Make1x1(dev, 255, 200, 0, 255);
            const auto pixels = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, nullptr, nullptr,
                                    Matrix::CreateTranslation(6.0f, 8.0f, 0.0f));
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
            });
            const Color atOrigin = pixels[0];
            const Color atTranslated = pixels[static_cast<std::size_t>(8) * 20 + 6];
            std::printf("       transformMatrix(translate): origin(0,0)=(%d,%d,%d,%d) "
                        "translated(6,8)=(%d,%d,%d,%d)\n",
                        atOrigin.getRProperty(), atOrigin.getGProperty(), atOrigin.getBProperty(),
                        atOrigin.getAProperty(), atTranslated.getRProperty(),
                        atTranslated.getGProperty(), atTranslated.getBProperty(),
                        atTranslated.getAProperty());
            check(atOrigin.getAProperty() < 50 &&
                  atTranslated.getRProperty() > 200 && atTranslated.getGProperty() > 150 &&
                  atTranslated.getBProperty() < 50,
                  "HTMLDOM-82a: a non-identity (translation) transformMatrix actually moves the "
                  "sprite to the matrix-predicted position, not left at its untransformed one");
        }

        // plan_html_dom.md HTMLDOM-82: a scale transformMatrix, the same "unambiguous, no sign
        // convention risk" reasoning as the translation check above. A 1x1 sprite under a
        // uniform 2x scale matrix must render as a 2x2 block, not a 1x1 one -- (1,1) falls
        // inside the scaled extent but outside the unscaled one, so it alone distinguishes
        // "matrix scale applied" from "matrix silently ignored".
        if (frame_ == 10)
        {
            Texture2D tex = Make1x1(dev, 0, 200, 255, 255);
            const auto pixels = ReadBackWholeTarget(*fontRt_, 20, 20, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, nullptr, nullptr, Matrix::CreateScale(2.0f));
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
            });
            const Color inside = pixels[static_cast<std::size_t>(1) * 20 + 1];
            const Color outside = pixels[static_cast<std::size_t>(3) * 20 + 3];
            std::printf("       transformMatrix(scale): inside(1,1)=(%d,%d,%d,%d) "
                        "outside(3,3)=(%d,%d,%d,%d)\n",
                        inside.getRProperty(), inside.getGProperty(), inside.getBProperty(),
                        inside.getAProperty(), outside.getRProperty(), outside.getGProperty(),
                        outside.getBProperty(), outside.getAProperty());
            check(inside.getBProperty() > 200 && inside.getGProperty() > 150 &&
                  outside.getAProperty() < 50,
                  "HTMLDOM-82b: a non-identity (scale) transformMatrix scales the sprite's "
                  "rendered footprint, not just its reported destinationRectangle");
        }

        // plan_html_dom.md HTMLDOM-83: render-target-as-Draw()-source, demonstrated visually in
        // htmldom_visual_demo.cpp but never asserted pixel-exact. Draw a distinct colour into
        // rtA_, unbind it, then Draw() rtA_ itself -- an ordinary RenderTarget2D : Texture2D --
        // as the source texture for a sprite rendered into rtB_. Proves the data-URL
        // regenerated-from-a-dirty-flag path (design decision 10) actually round-trips real
        // content, not just that the call sequence doesn't throw.
        if (frame_ == 11)
        {
            dev.SetRenderTarget(rtA_.get());
            dev.Clear(Color(255, 90, 20, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                spriteBatch_->Begin();
                spriteBatch_->Draw(*rtA_, Rectangle(2, 2, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
                spriteBatch_->End();
            });
            const Color sampled = pixels[static_cast<std::size_t>(4) * 20 + 4];
            const Color outside = pixels[static_cast<std::size_t>(15) * 20 + 15];
            std::printf("       RT-as-source: sampled(4,4)=(%d,%d,%d,%d) outside(15,15)=(%d,%d,%d,%d)\n",
                        sampled.getRProperty(), sampled.getGProperty(), sampled.getBProperty(),
                        sampled.getAProperty(), outside.getRProperty(), outside.getGProperty(),
                        outside.getBProperty(), outside.getAProperty());
            check(CloseEnough(sampled.getRProperty(), 255, 1) &&
                  CloseEnough(sampled.getGProperty(), 90, 1) &&
                  CloseEnough(sampled.getBProperty(), 20, 1) &&
                  sampled.getAProperty() == 255 && outside.getAProperty() < 50,
                  "HTMLDOM-83: a RenderTarget2D drawn into, then used as an ordinary Draw() "
                  "source texture, round-trips its real content exactly");
        }

        if (frame_ == 12)
        {
            // plan_html_dom.md HTMLDOM-96a: Texture2D::GetData on a PLAIN (non-render-target)
            // texture. Unlike RenderTarget2D::GetData (HTMLDOM-83/HtmlDomRenderTargetRenderer's own
            // Canvas2D getImageData), this path is entirely shared/renderer-agnostic code
            // (Texture2D::GetData reads from its own cpuPixels_ CPU-side shadow copy, never
            // touching the renderer at all) -- but it had never been exercised end-to-end under the
            // HTML_DOM Emscripten build specifically, only reviewed by reading the source. Four
            // distinct, unambiguous colours (one per texel, including a non-255 alpha) rule out
            // both a wrong-channel and a wrong-texel-order bug; CreateFromPixels/GetData is a
            // lossless path (no PNG/JPEG codec involved), so this is checked byte-exact, not with
            // CloseEnough's rounding tolerance.
            Texture2D plain = Texture2D::CreateFromPixels(dev, 2, 2, std::vector<std::uint8_t>{
                255, 0, 0, 255,     0, 255, 0, 200,
                0, 0, 255, 150,     255, 255, 0, 100,
            });
            std::vector<Color> px(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
            plain.GetData(px.data(), 0, 4);
            const bool plainMatches =
                px[0] == Color(255, 0, 0, 255) && px[1] == Color(0, 255, 0, 200) &&
                px[2] == Color(0, 0, 255, 150) && px[3] == Color(255, 255, 0, 100);
            std::printf("       plain GetData: (%d,%d,%d,%d) (%d,%d,%d,%d) (%d,%d,%d,%d) (%d,%d,%d,%d)\n",
                        px[0].getRProperty(), px[0].getGProperty(), px[0].getBProperty(), px[0].getAProperty(),
                        px[1].getRProperty(), px[1].getGProperty(), px[1].getBProperty(), px[1].getAProperty(),
                        px[2].getRProperty(), px[2].getGProperty(), px[2].getBProperty(), px[2].getAProperty(),
                        px[3].getRProperty(), px[3].getGProperty(), px[3].getBProperty(), px[3].getAProperty());
            check(plainMatches,
                  "HTMLDOM-96a: Texture2D::GetData on a plain (non-render-target) texture returns "
                  "the exact uploaded pixels, byte-for-byte, under the HTML_DOM renderer");

            // plan_html_dom.md HTMLDOM-96b: Texture2D::FromStream decode, closing the loop for this
            // renderer specifically. The decode itself (stb_image, via SaveAsPng/FromStream) is
            // fully renderer-agnostic shared code -- what had never been proven under HTML_DOM is
            // the two renderer-touching ends of that pipeline: the SOURCE texture's own upload
            // (SetData, exercised by CreateFromPixels below) and the DECODED texture's own upload
            // (FromStream's internal CreateFromPixels call), each going through this renderer's real
            // Emscripten canvas/PNG-data-URL machinery, not a native/desktop one.
            Texture2D src = Texture2D::CreateFromPixels(dev, 4, 4, std::vector<std::uint8_t>(
                static_cast<std::size_t>(4 * 4 * 4), 0));
            {
                std::vector<Color> solid(16, Color(30, 180, 220, 255));
                src.SetData(solid.data(), 16);
            }
            System::IO::MemoryStream writeStream;
            src.SaveAsPng(&writeStream, 4, 4);
            const auto pngBytes = writeStream.GetBuffer();

            System::IO::MemoryStream readStream(
                pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
            Texture2D decoded = Texture2D::FromStream(dev, readStream);

            std::vector<Color> decodedPx(16, Color(0xCD, 0xCD, 0xCD, 0xCD));
            decoded.GetData(decodedPx.data(), 0, 16);
            bool decodedMatches = decoded.getWidthProperty() == 4 && decoded.getHeightProperty() == 4;
            for (int i = 0; decodedMatches && i < 16; ++i)
            {
                decodedMatches = CloseEnough(decodedPx[i].getRProperty(), 30, 2) &&
                                 CloseEnough(decodedPx[i].getGProperty(), 180, 2) &&
                                 CloseEnough(decodedPx[i].getBProperty(), 220, 2) &&
                                 decodedPx[i].getAProperty() == 255;
            }
            std::printf("       FromStream decode: %dx%d px[0]=(%d,%d,%d,%d)\n",
                        decoded.getWidthProperty(), decoded.getHeightProperty(),
                        decodedPx[0].getRProperty(), decodedPx[0].getGProperty(),
                        decodedPx[0].getBProperty(), decodedPx[0].getAProperty());
            check(decodedMatches,
                  "HTMLDOM-96b: Texture2D::FromStream decodes a PNG produced by this renderer's own "
                  "SaveAsPng and re-uploads it correctly -- both the source and decoded textures "
                  "round-trip through the real HTML_DOM upload/readback path");
        }

        // plan_html_dom.md HTMLDOM-107: GraphicsDevice.Viewport applied on the Canvas2D
        // render-target-bound path -- previously ignored there entirely. A sub-rectangle Viewport
        // (4,4,10,10) set while rtB_ is bound must offset the sprite's own drawn position by
        // (4,4) within the target's absolute pixel space, matching the DOM path's own per-sprite
        // offset (htmldom_smoke_test.cpp HTMLDOM-107c). The "outside" sample point (1,1) is
        // deliberately inside where the sprite would have landed WITHOUT the viewport offset
        // (destRect (0,0,4,4)) -- if viewport were still ignored, this point would incorrectly
        // show the sprite's own colour instead of the target's transparent clear.
        if (frame_ == 13)
        {
            Texture2D tex = Make1x1(dev, 255, 0, 255, 255);
            const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                dev.setViewportProperty(Viewport(4, 4, 10, 10));
                spriteBatch_->Begin();
                spriteBatch_->Draw(tex, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
                dev.setViewportProperty(Viewport(0, 0, 20, 20));
            });
            const Color inside = pixels[static_cast<std::size_t>(6) * 20 + 6];
            const Color outside = pixels[static_cast<std::size_t>(1) * 20 + 1];
            std::printf("       RT viewport offset: inside(6,6)=(%d,%d,%d,%d) outside(1,1)=(%d,%d,%d,%d)\n",
                        inside.getRProperty(), inside.getGProperty(), inside.getBProperty(),
                        inside.getAProperty(), outside.getRProperty(), outside.getGProperty(),
                        outside.getBProperty(), outside.getAProperty());
            check(inside.getRProperty() > 200 && inside.getBProperty() > 200 &&
                  inside.getGProperty() < 50 && outside.getAProperty() < 50,
                  "HTMLDOM-107g: a sub-rectangle Viewport active while a RenderTarget2D is bound "
                  "offsets the sprite's OWN drawn position within the target's absolute pixel "
                  "space by the viewport's (X,Y) -- the Canvas2D render-target path previously "
                  "ignored viewport offset entirely");
        }

        // plan_html_dom.md HTMLDOM-102: real ctx.save()/rect()/clip() scissoring on the Canvas2D
        // render-target path, gated on RasterizerState.ScissorTestEnable -- previously this path
        // did not consult the scissor rect at all, regardless of the enable bit (htmldom_smoke_test
        // .cpp's own HTMLDOM-102a/b already cover the enable bit's effect on the DOM path/region
        // creation structurally; this is the Canvas2D path's real pixel proof).
        if (frame_ == 14)
        {
            RasterizerState scissorEnabled;
            scissorEnabled.setScissorTestEnableProperty(true);
            Texture2D tex = Make1x1(dev, 255, 0, 255, 255);

            const auto enabledPixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                dev.setScissorRectangleProperty(Rectangle(4, 4, 10, 10));
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, &scissorEnabled);
                // Covers the target's whole 20x20 extent -- "outside" the scissor rect is then
                // unambiguous: it's transparent only if the clip genuinely removed it.
                spriteBatch_->Draw(tex, Rectangle(0, 0, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
            });
            const Color insideEnabled = enabledPixels[static_cast<std::size_t>(8) * 20 + 8];
            const Color outsideEnabled = enabledPixels[static_cast<std::size_t>(1) * 20 + 1];
            std::printf("       RT scissor enabled: inside(8,8)=(%d,%d,%d,%d) "
                        "outside(1,1)=(%d,%d,%d,%d)\n",
                        insideEnabled.getRProperty(), insideEnabled.getGProperty(),
                        insideEnabled.getBProperty(), insideEnabled.getAProperty(),
                        outsideEnabled.getRProperty(), outsideEnabled.getGProperty(),
                        outsideEnabled.getBProperty(), outsideEnabled.getAProperty());
            check(insideEnabled.getRProperty() > 200 && insideEnabled.getBProperty() > 200 &&
                  outsideEnabled.getAProperty() < 50,
                  "HTMLDOM-102c: with ScissorTestEnable=true and a real ctx.clip(), a sprite drawn "
                  "into a bound RenderTarget2D is genuinely clipped to the scissor rect's own "
                  "bounds -- the Canvas2D path previously ignored the scissor rect entirely");

            const auto disabledPixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                dev.setScissorRectangleProperty(Rectangle(4, 4, 10, 10));
                spriteBatch_->Begin(); // plain Begin(): CullCounterClockwise's own default disables
                                       // scissor testing
                spriteBatch_->Draw(tex, Rectangle(0, 0, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
            });
            const Color outsideDisabled = disabledPixels[static_cast<std::size_t>(1) * 20 + 1];
            std::printf("       RT scissor disabled: outside(1,1)=(%d,%d,%d,%d)\n",
                        outsideDisabled.getRProperty(), outsideDisabled.getGProperty(),
                        outsideDisabled.getBProperty(), outsideDisabled.getAProperty());
            check(outsideDisabled.getAProperty() > 200,
                  "HTMLDOM-102d: the SAME scissor rect, with ScissorTestEnable=false, does NOT "
                  "clip -- the point that read transparent above (genuinely clipped out) now shows "
                  "the sprite's own colour");
        }

        // plan_html_dom.md HTMLDOM-113: Viewport AND ScissorRectangle active AT THE SAME TIME --
        // frames 13/14 above proved each independently, but never together, and HTMLDOM-107's own
        // design explicitly claims they are "independent absolute-render-target-space concepts"
        // (scissor is never relative to the viewport's own offset). Viewport(4,4,10,10) offsets the
        // sprite's drawn position by (4,4); ScissorRectangle(2,2,6,6) clips in the SAME absolute
        // target-pixel space the (now-offset) sprite lands in. Sprite drawn at logical (2,2) size
        // (6,6) -> actual absolute position (2+4,2+4)=(6,6) to (12,12) (viewport-offset applied);
        // scissor covers absolute [2,8)x[2,8). The two rects overlap only in [6,8)x[6,8) -- a
        // genuine partial intersection, not "everything visible" or "everything clipped", so all
        // three sample points below are independently meaningful.
        if (frame_ == 15)
        {
            RasterizerState scissorEnabled;
            scissorEnabled.setScissorTestEnableProperty(true);
            Texture2D tex = Make1x1(dev, 255, 0, 255, 255);

            const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                dev.setViewportProperty(Viewport(4, 4, 10, 10));
                dev.setScissorRectangleProperty(Rectangle(2, 2, 6, 6));
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, &scissorEnabled);
                spriteBatch_->Draw(tex, Rectangle(2, 2, 6, 6), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
                dev.setViewportProperty(Viewport(0, 0, 20, 20));
                dev.setScissorRectangleProperty(Rectangle(0, 0, 20, 20));
            });
            // (3,3): inside the sprite's UN-offset footprint (2,2)-(8,8) but OUTSIDE its real,
            // viewport-offset one (6,6)-(12,12) -- transparent here proves the viewport offset was
            // genuinely applied, not silently ignored while the scissor rect happened to still hide
            // the mistake.
            const Color viewportProof = pixels[static_cast<std::size_t>(3) * 20 + 3];
            // (7,7): inside BOTH the real (offset) sprite footprint AND the scissor rect's own
            // [2,8)x[2,8) bounds -- the one point where the sprite is actually visible.
            const Color bothVisible = pixels[static_cast<std::size_t>(7) * 20 + 7];
            // (10,10): inside the real (offset) sprite footprint but OUTSIDE the scissor rect --
            // transparent here proves the scissor clip is still independently enforced even with a
            // non-default viewport simultaneously active.
            const Color scissorProof = pixels[static_cast<std::size_t>(10) * 20 + 10];
            std::printf("       RT viewport+scissor together: viewportProof(3,3)=(%d,%d,%d,%d) "
                        "bothVisible(7,7)=(%d,%d,%d,%d) scissorProof(10,10)=(%d,%d,%d,%d)\n",
                        viewportProof.getRProperty(), viewportProof.getGProperty(),
                        viewportProof.getBProperty(), viewportProof.getAProperty(),
                        bothVisible.getRProperty(), bothVisible.getGProperty(),
                        bothVisible.getBProperty(), bothVisible.getAProperty(),
                        scissorProof.getRProperty(), scissorProof.getGProperty(),
                        scissorProof.getBProperty(), scissorProof.getAProperty());
            check(viewportProof.getAProperty() < 50,
                  "HTMLDOM-113: with a scissor rect ALSO active, the viewport's own (X,Y) offset "
                  "still genuinely moves the sprite's drawn position -- a point inside where the "
                  "sprite would have landed WITHOUT the offset is correctly empty");
            check(bothVisible.getRProperty() > 200 && bothVisible.getBProperty() > 200 &&
                  bothVisible.getAProperty() > 200,
                  "HTMLDOM-113: the point inside BOTH the viewport-offset sprite footprint AND the "
                  "scissor rect shows the sprite's own colour -- the two states compose correctly "
                  "rather than one masking the other");
            check(scissorProof.getAProperty() < 50,
                  "HTMLDOM-113: with a non-default viewport ALSO active, the scissor rect still "
                  "independently clips the (now-offset) sprite's footprint at its own absolute "
                  "bounds, matching HTMLDOM-107's own design claim that viewport and scissor are "
                  "independent absolute-space concepts, not one relative to the other");
        }

        // plan_html_dom.md HTMLDOM-97: symmetric TextureAddressMode::Mirror with an out-of-bounds
        // sourceRectangle, drawn pixel-exact for the first time (previously this threw). A 2x2
        // source Rectangle(0,0,4,4) sourceRect tiles it twice in each axis; under real mirror-repeat
        // the SECOND tile along each axis is the reflection of the first, unlike Wrap where it is an
        // identical repeat -- so this is a genuine discriminator between the two, not just "did
        // something get drawn". Expected grid hand-derived from mirror-repeat's standard "reflect at
        // every tile boundary" definition: effective source index for tiled position i (tile size 2)
        // is i for i<2, and (1-(i-2)) for i in [2,4) -- i.e. output index 0,1,2,3 samples source
        // index 0,1,1,0 on both axes.
        if (frame_ == 16)
        {
            SamplerState mirrorSampler;
            mirrorSampler.setAddressUProperty(TextureAddressMode::Mirror);
            mirrorSampler.setAddressVProperty(TextureAddressMode::Mirror);
            const auto pixels = ReadBackWholeTarget(*tileRt_, 4, 4, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &mirrorSampler,
                                    nullptr, nullptr);
                spriteBatch_->Draw(*tileTex_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
                spriteBatch_->End();
            });
            const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 4 + x]; };
            const Color red(255, 0, 0, 255), green(0, 255, 0, 255), blue(0, 0, 255, 255), yellow(255, 255, 0, 255);
            const auto matches = [](const Color& a, const Color& b) {
                return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
                       a.getBProperty() == b.getBProperty();
            };
            const Color expected[4][4] = {
                {red, green, green, red},
                {blue, yellow, yellow, blue},
                {blue, yellow, yellow, blue},
                {red, green, green, red},
            };
            bool mirrorMatches = true;
            for (int y = 0; mirrorMatches && y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                {
                    const Color actual = at(x, y);
                    if (!matches(actual, expected[y][x]))
                    {
                        std::printf("       mirror mismatch at (%d,%d): got (%d,%d,%d), want (%d,%d,%d)\n",
                                    x, y, actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                                    expected[y][x].getRProperty(), expected[y][x].getGProperty(),
                                    expected[y][x].getBProperty());
                        mirrorMatches = false;
                        break;
                    }
                }
            check(mirrorMatches,
                  "HTMLDOM-97a: symmetric TextureAddressMode::Mirror tiles the source with a real "
                  "reflection at every tile boundary, pixel-exact -- not a plain repeat (Wrap) and "
                  "not a throw");
        }

        // plan_html_dom.md HTMLDOM-97/HTMLDOM-104: mixed non-Mirror per-axis modes (U=Wrap,
        // V=Clamp). Same 2x2 source, same Rectangle(0,0,4,4) sourceRect (exceeds bounds on both
        // axes) -- U tiles (Wrap) while V clamps independently. HTMLDOM-104 correction: real Clamp
        // samples the nearest EDGE TEXEL for the out-of-bounds portion, it does not crop -- so rows
        // y=2,3 (2px past the texture's own 2px height) must replicate row 1 (blue,yellow), tiled
        // horizontally by U=Wrap exactly like row 1 itself, NOT come back transparent. (An earlier
        // version of this test asserted transparency there, matching the pre-HTMLDOM-104 crop-based
        // implementation -- the audit that reopened HTMLDOM-104 flagged this test oracle itself as
        // wrong, not just the implementation it was checking.)
        if (frame_ == 17)
        {
            SamplerState mixedSampler;
            mixedSampler.setAddressUProperty(TextureAddressMode::Wrap);
            mixedSampler.setAddressVProperty(TextureAddressMode::Clamp);
            const auto pixels = ReadBackWholeTarget(*tileRt_, 4, 4, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &mixedSampler,
                                    nullptr, nullptr);
                spriteBatch_->Draw(*tileTex_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
                spriteBatch_->End();
            });
            const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 4 + x]; };
            const Color red(255, 0, 0, 255), green(0, 255, 0, 255), blue(0, 0, 255, 255), yellow(255, 255, 0, 255);
            const auto matches = [](const Color& a, const Color& b) {
                return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
                       a.getBProperty() == b.getBProperty();
            };
            bool topRowsTile = matches(at(0, 0), red) && matches(at(1, 0), green) &&
                               matches(at(2, 0), red) && matches(at(3, 0), green) &&
                               matches(at(0, 1), blue) && matches(at(1, 1), yellow) &&
                               matches(at(2, 1), blue) && matches(at(3, 1), yellow);
            bool bottomRowsClampToEdge = matches(at(0, 2), blue) && matches(at(1, 2), yellow) &&
                                         matches(at(2, 2), blue) && matches(at(3, 2), yellow) &&
                                         matches(at(0, 3), blue) && matches(at(1, 3), yellow) &&
                                         matches(at(2, 3), blue) && matches(at(3, 3), yellow) &&
                                         at(0, 2).getAProperty() > 200 && at(0, 3).getAProperty() > 200;
            std::printf("       mixed axes: row0=(%d,%d,%d)(%d,%d,%d) row2=(%d,%d,%d,a=%d) "
                        "row3=(%d,%d,%d,a=%d)\n",
                        at(0, 0).getRProperty(), at(0, 0).getGProperty(), at(0, 0).getBProperty(),
                        at(2, 0).getRProperty(), at(2, 0).getGProperty(), at(2, 0).getBProperty(),
                        at(0, 2).getRProperty(), at(0, 2).getGProperty(), at(0, 2).getBProperty(),
                        at(0, 2).getAProperty(), at(0, 3).getRProperty(), at(0, 3).getGProperty(),
                        at(0, 3).getBProperty(), at(0, 3).getAProperty());
            check(topRowsTile && bottomRowsClampToEdge,
                  "HTMLDOM-97b/HTMLDOM-104: mixed U=Wrap/V=Clamp tiles the U axis exactly like "
                  "symmetric Wrap while the V axis clamps independently -- overflow rows replicate "
                  "the nearest edge row (still U-tiled), not left transparent");
        }

        // plan_html_dom.md HTMLDOM-104: the rest of this task's own pixel-verification bar -- a
        // sourceRectangle entirely outside the texture (checked under BOTH point and linear
        // filtering, confirming the edge-extension is exact regardless -- this specific pairing is
        // what caught a real bug while building this task: with smoothing left on, the browser's own
        // GPU-accelerated drawImage bilinear-sampled slightly past a 1-texel padding slice's own
        // edge, bleeding in the adjacent texel's colour), and a scaled, tinted, one-axis overflow
        // with hand-derived exact expected colours.
        if (frame_ == 18)
        {
            // Entirely outside the 2x2 texture on both axes (sourceX/Y=10, texture is 2x2) -- every
            // sampled texel clamps to the bottom-right corner texel (yellow).
            const Color yellow(255, 255, 0, 255);
            const auto sampleCorner = [&](SamplerState* sampler) {
                const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, sampler,
                                        nullptr, nullptr);
                    spriteBatch_->Draw(*tileTex_, Rectangle(0, 0, 8, 8), Rectangle(10, 10, 4, 4), Color::White);
                    spriteBatch_->End();
                });
                return pixels[static_cast<std::size_t>(4) * 20 + 4];
            };
            const Color linearResult = sampleCorner(const_cast<SamplerState*>(&SamplerState::LinearClamp));
            const Color pointResult = sampleCorner(const_cast<SamplerState*>(&SamplerState::PointClamp));
            std::printf("       fully-outside corner: linear=(%d,%d,%d,%d) point=(%d,%d,%d,%d)\n",
                        linearResult.getRProperty(), linearResult.getGProperty(),
                        linearResult.getBProperty(), linearResult.getAProperty(),
                        pointResult.getRProperty(), pointResult.getGProperty(),
                        pointResult.getBProperty(), pointResult.getAProperty());
            check(linearResult == yellow && pointResult == yellow,
                  "HTMLDOM-104: a sourceRectangle entirely outside the texture clamps every sampled "
                  "texel to the nearest (bottom-right corner) texel, identically under both point "
                  "and linear filtering -- a real bug (GPU bilinear edge-bleed from the adjacent "
                  "texel under linear filtering specifically) was caught and fixed by this exact "
                  "point-vs-linear comparison while building this task");

            // Scale (2x) + tint + a modest one-axis (V) overflow: sourceRect (0,0,2,3) on the 2x2
            // texture exceeds its 2px height by 1 on the bottom edge only. Dest is scaled 2x, so
            // source rows [0,2) (the real texture) land on dest rows [0,4), and the 1-row overflow
            // lands on dest rows [4,6) -- deep samples at dest y=5 must show the BOTTOM edge row
            // (blue, yellow), tinted, not the top row and not transparent.
            const Color tint(128, 255, 255, 255);
            const auto tintedPixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                                    const_cast<SamplerState*>(&SamplerState::LinearClamp), nullptr, nullptr);
                spriteBatch_->Draw(*tileTex_, Rectangle(0, 0, 4, 6), Rectangle(0, 0, 2, 3), tint);
                spriteBatch_->End();
            });
            const Color tintedBlue = tintedPixels[static_cast<std::size_t>(5) * 20 + 0];
            const Color tintedYellow = tintedPixels[static_cast<std::size_t>(5) * 20 + 3];
            std::printf("       scaled+tinted overflow: blue=(%d,%d,%d,%d) yellow=(%d,%d,%d,%d)\n",
                        tintedBlue.getRProperty(), tintedBlue.getGProperty(), tintedBlue.getBProperty(),
                        tintedBlue.getAProperty(), tintedYellow.getRProperty(), tintedYellow.getGProperty(),
                        tintedYellow.getBProperty(), tintedYellow.getAProperty());
            // Blue (0,0,255) is unaffected by this tint (its only non-zero channel, B, is tinted by
            // 255/255=1). Yellow (255,255,0) tinted by (128,255,255)/255 -> (128,255,0) exactly
            // (255*128 == 32640 == 128*255, no rounding involved).
            check(CloseEnough(tintedBlue.getRProperty(), 0, 1) && CloseEnough(tintedBlue.getGProperty(), 0, 1) &&
                  CloseEnough(tintedBlue.getBProperty(), 255, 1) && tintedBlue.getAProperty() == 255 &&
                  CloseEnough(tintedYellow.getRProperty(), 128, 1) && CloseEnough(tintedYellow.getGProperty(), 255, 1) &&
                  CloseEnough(tintedYellow.getBProperty(), 0, 1) && tintedYellow.getAProperty() == 255,
                  "HTMLDOM-104: a scaled (2x), tinted draw with a one-axis Clamp overflow shows the "
                  "correctly TINTED edge-replicated row in the overflow region -- tint and scale both "
                  "compose correctly with the edge extension, not just an untinted 1:1 draw");
        }

        // plan_html_dom.md HTMLDOM-105: re-derived each preset's RAW factor-based RGBA equation
        // (colour AND alpha, independent of any browser) -- see HtmlDomState.hpp's own doc comment
        // for the full derivation. AlphaBlend's alpha factor is `One`, matching CSS's own Porter-Duff
        // "over" alpha exactly, so it needs no new check here (already pixel-verified translucent,
        // HTMLDOM-85). NonPremultiplied and Additive both use `SourceAlpha` as their OWN alpha
        // factor, so their real alpha equations SQUARE the source alpha's own contribution -- neither
        // `source-over` nor `lighter` can reproduce that (no per-channel blend-factor model in CSS),
        // an accepted architectural limitation for TRANSLUCENT sources specifically (opaque sources,
        // src_a=255, make the two formulas coincide exactly, which is why every earlier opaque-only
        // test -- HTMLDOM-87 included -- could never have caught this). These checks MEASURE this
        // renderer's actual (CSS-native, non-squared) result with real translucent source AND
        // destination data and raw RGBA readback, documenting the XNA-exact delta by hand rather than
        // asserting pixel-exactness that is not architecturally achievable here.
        if (frame_ == 19)
        {
            // NonPremultiplied, single translucent draw onto a transparent target: CSS-native
            // source-over onto fully-transparent dst always yields the source unchanged (a standard
            // Porter-Duff identity), including alpha. XNA's own squared formula would instead give
            // alpha = round(128*128/255) = 64 here -- a real, measurable divergence on the very
            // FIRST draw, not just a multi-draw accumulation effect.
            const Color npSingle = [&] {
                Texture2D tex = Make1x1(dev, 200, 100, 50, 128);
                const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied,
                                        nullptr, nullptr, nullptr);
                    spriteBatch_->Draw(tex, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                    spriteBatch_->End();
                });
                return pixels[static_cast<std::size_t>(2) * 20 + 2];
            }();
            std::printf("       NonPremultiplied single-draw-onto-transparent alpha: measured=%d "
                        "(CSS-native expects 128, hypothetical XNA-exact squared formula gives 64)\n",
                        npSingle.getAProperty());
            check(CloseEnough(npSingle.getRProperty(), 200, 1) && CloseEnough(npSingle.getGProperty(), 100, 1) &&
                  CloseEnough(npSingle.getBProperty(), 50, 1) && CloseEnough(npSingle.getAProperty(), 128, 1),
                  "HTMLDOM-105: NonPremultiplied with a translucent source drawn onto a transparent "
                  "target reproduces the source's colour AND alpha unchanged (this renderer's actual, "
                  "CSS-native source-over result) -- XNA's own literal alphaSrcBlend=SourceAlpha "
                  "factor would instead SQUARE the source alpha here (~64, not 128), a documented "
                  "architectural gap this renderer does not reproduce");

            // Additive, two sequential translucent draws (the ticket's own reported scenario: two
            // 50%-alpha 'lighter' draws) -- reproduces the exact numbers HTMLDOM-105's own audit
            // measured: this renderer's real CSS-native result is (128,0,128,255); XNA's own squared-
            // alpha formula, applied recursively across both draws, would give (128,0,128,~128).
            const Color addTwoDraw = [&] {
                Texture2D blue = Make1x1(dev, 0, 0, 255, 128);
                Texture2D red = Make1x1(dev, 255, 0, 0, 128);
                const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Additive, nullptr, nullptr, nullptr);
                    spriteBatch_->Draw(blue, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                    spriteBatch_->End();
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Additive, nullptr, nullptr, nullptr);
                    spriteBatch_->Draw(red, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                    spriteBatch_->End();
                });
                return pixels[static_cast<std::size_t>(2) * 20 + 2];
            }();
            std::printf("       Additive two-translucent-draw result: (%d,%d,%d,%d) (CSS-native "
                        "expects (128,0,128,255); hypothetical XNA-exact squared formula gives "
                        "(128,0,128,~128))\n",
                        addTwoDraw.getRProperty(), addTwoDraw.getGProperty(), addTwoDraw.getBProperty(),
                        addTwoDraw.getAProperty());
            check(CloseEnough(addTwoDraw.getRProperty(), 128, 1) && addTwoDraw.getGProperty() == 0 &&
                  CloseEnough(addTwoDraw.getBProperty(), 128, 1) && CloseEnough(addTwoDraw.getAProperty(), 255, 1),
                  "HTMLDOM-105: Additive with two sequential translucent draws sums colour exactly "
                  "(128,0,128) and clamps alpha via CSS-native Porter-Duff 'plus' to 255 -- XNA's own "
                  "squared alphaSrcBlend=SourceAlpha factor, applied recursively across both draws, "
                  "would instead give alpha~128, a documented architectural gap this renderer does "
                  "not reproduce");

            // Zero-alpha draw: both formulas trivially agree here (0*0=0), so this is a plain
            // regression sanity check, not a divergence demonstration -- a zero-alpha sprite must
            // leave an already-opaque destination completely untouched, byte-for-byte.
            const Color zeroAlphaBase(10, 20, 30, 255);
            const Color zeroAlphaResult = [&] {
                Texture2D opaque = Make1x1(dev, 10, 20, 30, 255);
                Texture2D invisible = Make1x1(dev, 255, 255, 255, 0);
                const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
                    spriteBatch_->Draw(opaque, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                    spriteBatch_->End();
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied,
                                        nullptr, nullptr, nullptr);
                    spriteBatch_->Draw(invisible, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                    spriteBatch_->End();
                });
                return pixels[static_cast<std::size_t>(2) * 20 + 2];
            }();
            std::printf("       zero-alpha-draw result: (%d,%d,%d,%d)\n", zeroAlphaResult.getRProperty(),
                        zeroAlphaResult.getGProperty(), zeroAlphaResult.getBProperty(),
                        zeroAlphaResult.getAProperty());
            check(zeroAlphaResult == zeroAlphaBase,
                  "HTMLDOM-105: a fully transparent (alpha=0) NonPremultiplied draw leaves an "
                  "already-opaque destination byte-for-byte unchanged -- both the CSS-native and the "
                  "XNA-exact alpha formulas trivially agree at src_a=0, so this is a plain regression "
                  "check, not a divergence demonstration");

            // Tint ALPHA (not texture alpha) must drive the same (documented, CSS-native) formula --
            // an opaque texel (alpha=255) tinted with Color(255,255,255,128) must behave identically
            // to the texture-alpha case above, proving tint alpha feeds the same effective src_a.
            const Color npTintAlpha = [&] {
                Texture2D opaqueTex = Make1x1(dev, 200, 100, 50, 255);
                const auto pixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied,
                                        nullptr, nullptr, nullptr);
                    spriteBatch_->Draw(opaqueTex, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1),
                                       Color(255, 255, 255, 128));
                    spriteBatch_->End();
                });
                return pixels[static_cast<std::size_t>(2) * 20 + 2];
            }();
            std::printf("       NonPremultiplied tint-alpha result: (%d,%d,%d,%d)\n",
                        npTintAlpha.getRProperty(), npTintAlpha.getGProperty(), npTintAlpha.getBProperty(),
                        npTintAlpha.getAProperty());
            check(CloseEnough(npTintAlpha.getRProperty(), 200, 1) && CloseEnough(npTintAlpha.getGProperty(), 100, 1) &&
                  CloseEnough(npTintAlpha.getBProperty(), 50, 1) && CloseEnough(npTintAlpha.getAProperty(), 128, 1),
                  "HTMLDOM-105: the sprite's own TINT alpha (not texture alpha) drives the identical "
                  "effective src_a used above -- an opaque (alpha=255) texel tinted to alpha=128 "
                  "reads back exactly like the texture-alpha=128 case, confirming tint alpha isn't "
                  "handled by some separate, inconsistent code path");
        }

        // plan_html_dom.md HTMLDOM-106 (reopens HTMLDOM-52/53/83/85): a render target's own backing
        // canvas is ALWAYS straight (non-premultiplied) alpha on readback -- Canvas2D's getImageData
        // contract, confirmed by leg 1 below -- regardless of which BlendState drew the content.
        // cnaDomGetVariant's mode 1 ("un-premultiplied") exists to correct an UPLOADED texture's
        // bytes, which the AlphaBlend contract assumes are the game's own ALREADY-premultiplied asset
        // data; applying that SAME division to a render target's already-straight bytes divides by
        // alpha a second time. HTMLDOM-83's existing round-trip used fully opaque content (alpha=255),
        // where the un-premultiply branch's own `aa > 0 && aa < 255` guard never even fires -- unable
        // to detect this by construction. This frame uses a genuinely translucent source, mirroring
        // HTMLDOM-85's own premultiplied texel (255,100,50) at alpha=128 -> premultiplied
        // (128,50,25,128), so the write, readback, and resample legs below are all hand-derived from
        // the exact same already-verified numbers.
        if (frame_ == 20)
        {
            // Leg 1 (write + readback): draw the premultiplied texel into rtA_ under AlphaBlend, onto
            // rtA_'s own transparent clear. Reused HTMLDOM-85 math: an AlphaBlend draw onto a fully
            // transparent destination reduces to the un-premultiplied source, so rtA_ should now hold
            // straight (255,100,50,128) -- confirming getImageData's straight-alpha contract applies
            // to a render target's OWN canvas exactly as it does to the scratch target HTMLDOM-85 used.
            Texture2D premultTex = Make1x1(dev, 128, 50, 25, 128);
            const auto rtAPixels = ReadBackWholeTarget(*rtA_, 4, 4, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr);
                spriteBatch_->Draw(premultTex, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
            });
            const Color rtAColor = rtAPixels[0];
            std::printf("       RT A readback (write+readback leg): (%d,%d,%d,%d) want ~(255,100,50,128)\n",
                        rtAColor.getRProperty(), rtAColor.getGProperty(), rtAColor.getBProperty(),
                        rtAColor.getAProperty());
            check(CloseEnough(rtAColor.getRProperty(), 255, 2) && CloseEnough(rtAColor.getGProperty(), 100, 2) &&
                  CloseEnough(rtAColor.getBProperty(), 50, 2) && CloseEnough(rtAColor.getAProperty(), 128, 2),
                  "HTMLDOM-106: a render target's own GetData readback after an AlphaBlend draw is "
                  "straight (non-premultiplied) alpha, matching Canvas2D's getImageData contract");

            // Leg 2 (resample onto a TRANSPARENT destination): rtA_ (straight (255,100,50,128)) drawn
            // as an ordinary Draw() source into rtB_ under AlphaBlend. AlphaBlend-compositing an
            // already-straight source onto a fresh-transparent destination is representation-
            // preserving (there is nothing underneath to blend with), so the correct result is rtA_'s
            // OWN colour, unchanged: (255,100,50,128). The pre-fix double-division bug instead first
            // WRONGLY un-premultiplies rtA_'s straight bytes a second time (inv=255/128, clamped) to
            // (255,199,100), then composites that -- a visibly different, measurably wrong result.
            const auto transparentDestPixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr);
                spriteBatch_->Draw(*rtA_, Rectangle(2, 2, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
                spriteBatch_->End();
            });
            const Color transparentDestColor = transparentDestPixels[static_cast<std::size_t>(4) * 20 + 4];
            std::printf("       RT A -> RT B, AlphaBlend over transparent: (%d,%d,%d,%d) want ~(255,100,50,128)\n",
                        transparentDestColor.getRProperty(), transparentDestColor.getGProperty(),
                        transparentDestColor.getBProperty(), transparentDestColor.getAProperty());
            check(CloseEnough(transparentDestColor.getRProperty(), 255, 2) &&
                  CloseEnough(transparentDestColor.getGProperty(), 100, 2) &&
                  CloseEnough(transparentDestColor.getBProperty(), 50, 2) &&
                  CloseEnough(transparentDestColor.getAProperty(), 128, 2),
                  "HTMLDOM-106: sampling a translucent render target as a Draw() source under "
                  "AlphaBlend, onto a transparent destination, reproduces the render target's own "
                  "straight colour exactly -- not divided by alpha a second time");

            // Leg 3 (resample onto a NON-transparent destination): same rtA_ source, this time
            // composited over an opaque pre-filled rtB_ background (0,0,200,255). Hand-derived via the
            // browser's own real source-over algebra (a = 128/255): result = src*a + dst*(1-a) =
            // (255*a, 100*a, 50*a + 200*(1-a)) = (128, ~50, ~125), alpha stays saturated at 255 since
            // the destination was already opaque. The pre-fix bug's wrongly-divided (255,199,100)
            // source instead composites to (128, ~100, ~150) -- a clearly different, wrong G/B pair.
            const auto opaqueDestPixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                Texture2D bg = Make1x1(dev, 0, 0, 200, 255);
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
                spriteBatch_->Draw(bg, Rectangle(0, 0, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->End();
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr);
                spriteBatch_->Draw(*rtA_, Rectangle(2, 2, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
                spriteBatch_->End();
            });
            const Color opaqueDestColor = opaqueDestPixels[static_cast<std::size_t>(4) * 20 + 4];
            std::printf("       RT A -> RT B, AlphaBlend over opaque bg: (%d,%d,%d,%d) want ~(128,50,125,255)\n",
                        opaqueDestColor.getRProperty(), opaqueDestColor.getGProperty(),
                        opaqueDestColor.getBProperty(), opaqueDestColor.getAProperty());
            check(CloseEnough(opaqueDestColor.getRProperty(), 128, 3) &&
                  CloseEnough(opaqueDestColor.getGProperty(), 50, 3) &&
                  CloseEnough(opaqueDestColor.getBProperty(), 125, 3) &&
                  CloseEnough(opaqueDestColor.getAProperty(), 255, 3),
                  "HTMLDOM-106: sampling the same translucent render target under AlphaBlend onto a "
                  "NON-transparent destination blends by its real straight colour, matching the "
                  "browser's own source-over algebra exactly -- not the double-divided value");

            // Leg 4 (a second preset, confirming the fix is scoped correctly): NonPremultiplied never
            // requested mode 1 in the first place (see HtmlDomTextureRenderer.cpp's own mode table), so
            // sampling rtA_ under NonPremultiplied onto a transparent rtB_ was never affected by this
            // bug and must still reproduce the identical (255,100,50,128) result as leg 2 -- confirming
            // the isRenderTarget-aware downgrade in cnaDomGetVariant is additive, not a behaviour
            // change for a preset that was already correct.
            const auto nonPremultDestPixels = ReadBackWholeTarget(*rtB_, 20, 20, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, nullptr, nullptr, nullptr);
                spriteBatch_->Draw(*rtA_, Rectangle(2, 2, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
                spriteBatch_->End();
            });
            const Color nonPremultDestColor = nonPremultDestPixels[static_cast<std::size_t>(4) * 20 + 4];
            std::printf("       RT A -> RT B, NonPremultiplied over transparent: (%d,%d,%d,%d) want ~(255,100,50,128)\n",
                        nonPremultDestColor.getRProperty(), nonPremultDestColor.getGProperty(),
                        nonPremultDestColor.getBProperty(), nonPremultDestColor.getAProperty());
            check(CloseEnough(nonPremultDestColor.getRProperty(), 255, 2) &&
                  CloseEnough(nonPremultDestColor.getGProperty(), 100, 2) &&
                  CloseEnough(nonPremultDestColor.getBProperty(), 50, 2) &&
                  CloseEnough(nonPremultDestColor.getAProperty(), 128, 2),
                  "HTMLDOM-106: NonPremultiplied sampling of the same render target was already "
                  "unaffected by the mode-1 double-division bug and remains correct after the fix");
        }

        if (frame_ == 21)
        {
            // plan_html_dom.md HTMLDOM-119: Wrap/Mirror phase alignment for a NEGATIVE source
            // rectangle origin -- every existing Wrap/Mirror test in this suite starts its source
            // rect at (0,0); neither cnaDomResolveClampVariant's tiled branch nor
            // cnaDomGetMirrorVariant normalizes sx/sy at all (both hand the browser a raw,
            // un-normalized offset and rely on native infinite background-repeat/CanvasPattern
            // tiling), so a negative origin should be architecturally identical, just phase-shifted
            // -- confirmed here rather than left untested. Shift of -1 (not a multiple of the 2px
            // Wrap period or the 4px Mirror period) so the result is genuinely different from --
            // not accidentally identical to -- the existing (0,0,4,4) checks above, and hand-derived
            // from the real tiling formula rather than compared against this renderer's own output.
            {
                SamplerState wrapSampler;
                wrapSampler.setAddressUProperty(TextureAddressMode::Wrap);
                wrapSampler.setAddressVProperty(TextureAddressMode::Wrap);
                const auto pixels = ReadBackWholeTarget(*tileRt_, 4, 4, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &wrapSampler,
                                        nullptr, nullptr);
                    spriteBatch_->Draw(*tileTex_, Rectangle(0, 0, 4, 4), Rectangle(-1, -1, 4, 4), Color::White);
                    spriteBatch_->End();
                });
                const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 4 + x]; };
                const Color red(255, 0, 0, 255), green(0, 255, 0, 255), blue(0, 0, 255, 255), yellow(255, 255, 0, 255);
                const auto matches = [](const Color& a, const Color& b) {
                    return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
                           a.getBProperty() == b.getBProperty();
                };
                // Hand-derived: real Wrap samples texel ((sourceX+elementX) mod texW). With
                // sourceX=-1 and texW=2, columns 0..3 map to texel-x [1,0,1,0]; same formula on Y
                // gives rows [1,0,1,0]. texel(x,y): (0,0)=red (1,0)=green (0,1)=blue (1,1)=yellow.
                const Color expected[4][4] = {
                    {yellow, blue, yellow, blue},
                    {green, red, green, red},
                    {yellow, blue, yellow, blue},
                    {green, red, green, red},
                };
                bool wrapNegMatches = true;
                for (int y = 0; wrapNegMatches && y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                        if (!matches(at(x, y), expected[y][x])) { wrapNegMatches = false; break; }
                check(wrapNegMatches,
                      "HTMLDOM-119: TextureAddressMode::Wrap with a NEGATIVE source rectangle "
                      "origin (-1,-1) tiles at the correct phase, hand-derived from "
                      "(sourceX+elementX) mod texW -- not merely happening to look right at the "
                      "(0,0) origin every other test in this suite uses");
            }
            {
                SamplerState mirrorSampler;
                mirrorSampler.setAddressUProperty(TextureAddressMode::Mirror);
                mirrorSampler.setAddressVProperty(TextureAddressMode::Mirror);
                const auto pixels = ReadBackWholeTarget(*tileRt_, 4, 4, [&] {
                    spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &mirrorSampler,
                                        nullptr, nullptr);
                    spriteBatch_->Draw(*tileTex_, Rectangle(0, 0, 4, 4), Rectangle(-1, -1, 4, 4), Color::White);
                    spriteBatch_->End();
                });
                const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 4 + x]; };
                const Color red(255, 0, 0, 255), green(0, 255, 0, 255), blue(0, 0, 255, 255), yellow(255, 255, 0, 255);
                const auto matches = [](const Color& a, const Color& b) {
                    return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
                           a.getBProperty() == b.getBProperty();
                };
                // Hand-derived from the real GL_MIRRORED_REPEAT formula (m=((i mod 2w)+2w) mod 2w;
                // result = m<w ? m : 2w-1-m) with sourceX=-1, w=2: columns 0..3 map to texel-x
                // [0,0,1,1]; same on Y gives rows [0,0,1,1] -- solid 2x2 blocks, not the
                // checkerboard the (0,0) origin produces.
                const Color expected[4][4] = {
                    {red, red, green, green},
                    {red, red, green, green},
                    {blue, blue, yellow, yellow},
                    {blue, blue, yellow, yellow},
                };
                bool mirrorNegMatches = true;
                for (int y = 0; mirrorNegMatches && y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                        if (!matches(at(x, y), expected[y][x])) { mirrorNegMatches = false; break; }
                check(mirrorNegMatches,
                      "HTMLDOM-119: TextureAddressMode::Mirror with a NEGATIVE source rectangle "
                      "origin (-1,-1) reflects at the correct phase, hand-derived from the real "
                      "GL_MIRRORED_REPEAT formula -- solid 2x2 blocks, not the (0,0)-origin "
                      "checkerboard every other Mirror test in this suite uses");
            }

            // plan_html_dom.md HTMLDOM-119: the Canvas2D-path half of the atlas-edge-bleed /
            // fractional-scale scenario -- see the DOM-path half in
            // scripts/htmldom-browser-test.mjs's own verifyPixelVerificationScreenshot, which reads
            // back the SAME scenario drawn to the real backbuffer below via a real screenshot
            // (design decision 11: no in-page DOM readback exists). Both checked against the SAME
            // hand-derived "pure red, no bleed" expectation independently, rather than compared
            // against each other -- a cross-path A==B comparison could pass even if BOTH paths bled
            // identically, which would be a false sense of confidence.
            SamplerState linearSampler;
            linearSampler.setFilterProperty(TextureFilter::Linear);
            const auto atlasPixels = ReadBackWholeTarget(*atlasRt_, 44, 44, [&] {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linearSampler,
                                    nullptr, nullptr);
                spriteBatch_->Draw(*atlasTexture_, Rectangle(2, 2, 39, 39), Rectangle(0, 0, 2, 4), Color::White);
                spriteBatch_->End();
            });
            const auto atlasAt = [&](int x, int y) { return atlasPixels[static_cast<std::size_t>(y) * 44 + x]; };
            const Color atlasInterior = atlasAt(12, 21);
            const Color atlasNearEdge = atlasAt(39, 21);
            std::printf("       HTMLDOM-119 Canvas2D-path atlas interior=(%d,%d,%d) nearEdge=(%d,%d,%d)\n",
                        atlasInterior.getRProperty(), atlasInterior.getGProperty(), atlasInterior.getBProperty(),
                        atlasNearEdge.getRProperty(), atlasNearEdge.getGProperty(), atlasNearEdge.getBProperty());
            check(CloseEnough(atlasInterior.getRProperty(), 255, 20) &&
                  CloseEnough(atlasInterior.getGProperty(), 0, 20) &&
                  CloseEnough(atlasInterior.getBProperty(), 0, 20),
                  "HTMLDOM-119: Canvas2D path -- a large fractional scale (19.5x/9.75x) with linear "
                  "filtering, sampled well inside the drawn red region, stays pure red");
            // plan_html_dom.md HTMLDOM-119: measured first, NOT assumed clean -- this sample DOES
            // pick up real bilinear bleed from the atlas's adjacent (undrawn) blue region, and that
            // is the CORRECT, hardware-matching result, not a bug: real D3D/XNA hardware sampling
            // an unpadded atlas sub-rectangle under LinearWrap/LinearClamp exhibits the identical
            // texel bleed (why real games pad their own atlases) -- this renderer faithfully
            // reproducing it is what "correct" means here. Hand-derived exactly, not just measured
            // once and pinned: local x=37 maps to source-space x=37/19.5=1.897, which is
            // 0.397 texels past the last drawn (red) texel's own centre (1.5) toward the
            // NEXT (undrawn, blue) texel's centre (2.5) -- linear interpolation with that weight
            // gives R=255*(1-0.397)=~154, B=255*0.397=~101, matching the measured (147,0,108)
            // within ordinary cross-technology bilinear-coordinate rounding.
            check(CloseEnough(atlasNearEdge.getRProperty(), 154, 25) &&
                  CloseEnough(atlasNearEdge.getGProperty(), 0, 10) &&
                  CloseEnough(atlasNearEdge.getBProperty(), 101, 25),
                  "HTMLDOM-119: Canvas2D path -- sampled 2px from the drawn region's own right "
                  "edge shows REAL bilinear bleed from the atlas's adjacent undrawn region, at "
                  "exactly the hand-derived interpolation weight -- matching real GPU hardware "
                  "sampling an unpadded atlas, not a CNA-specific corruption");

            // The DOM-path half of the SAME scenario -- identical source/dest rects and sampler,
            // drawn to the real backbuffer (no render target bound) instead of atlasRt_. Left as
            // the very LAST thing this whole test draws, deliberately: nothing runs after this
            // frame, so it survives for scripts/htmldom-browser-test.mjs's own screenshot-based
            // verifyPixelVerificationScreenshot to sample -- the same "last draw survives" shape
            // htmldom_smoke_test.cpp's own HTMLDOM-101 screenshot check already relies on.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linearSampler,
                                nullptr, nullptr);
            spriteBatch_->Draw(*atlasTexture_, Rectangle(2, 2, 39, 39), Rectangle(0, 0, 2, 4), Color::White);
            spriteBatch_->End();

            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    HtmlDomPixelVerificationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    // Heap-allocated, not a local: emscripten_set_main_loop(..., simulateInfiniteLoop=1) unwinds
    // this stack frame via a JS-level throw (see docs/emscripten-mainloop-game-lifetime.md) --
    // a stack-local Game here would have its storage reclaimed while the loop callback still
    // holds a raw pointer to it.
    HtmlDomPixelVerificationTest* game = new HtmlDomPixelVerificationTest();
    game->Run();
    return game->getResult();
}
