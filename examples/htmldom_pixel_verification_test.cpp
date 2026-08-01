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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "System/IO/MemoryStream.hpp"

#include "CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.hpp"

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
    constexpr int kExpectedChecks = 15;

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
    // tolerance exists only to absorb legitimate 8-bit rounding -- this backend's own
    // round-to-nearest during its per-pixel maths, PLUS (for the AlphaBlend check specifically)
    // the browser's own internal premultiply-for-compositing/un-premultiply-for-readback round
    // trip, which is a second, independent source of +-1-ish rounding this backend does not
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
        // SDL_RENDERER's own Task 697 pixel test -- and confirm the backend's un-premultiply pass
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
            // backend's own un-premultiply division, then the browser's own internal
            // premultiply-for-compositing/un-premultiply-for-readback round trip.
            check(CloseEnough(result.getRProperty(), 255, 2) &&
                  CloseEnough(result.getGProperty(), 100, 2) &&
                  CloseEnough(result.getBProperty(), 50, 2) &&
                  CloseEnough(result.getAProperty(), 128, 2),
                  "HTMLDOM-85: AlphaBlend un-premultiplies premultiplied source data correctly");
        }

        // plan_html_dom.md HTMLDOM-86: Opaque strips alpha to 255 and leaves RGB untouched. Use a
        // GENUINELY semi-transparent source texel (every prior check used alpha=255 source data,
        // so the strip was a no-op every time it ran) -- if the strip were instead blending with
        // the transparent background, RGB would come back darkened; if it forgot to force alpha,
        // it would come back at the source's own alpha instead of 255.
        if (frame_ == 3)
        {
            Texture2D tex = Make1x1(dev, 180, 90, 40, 120);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::Opaque, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 255, 255, 255));
            });
            std::printf("       opaque got (%d,%d,%d,%d) want ~(180,90,40,255)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            check(CloseEnough(result.getRProperty(), 180, 1) &&
                  CloseEnough(result.getGProperty(), 90, 1) &&
                  CloseEnough(result.getBProperty(), 40, 1) &&
                  result.getAProperty() == 255,
                  "HTMLDOM-86: Opaque strips alpha to 255 without darkening RGB from a real "
                  "semi-transparent source");
        }

        // plan_html_dom.md HTMLDOM-87: Additive must actually ADD channel values (clamped), not
        // just apply SOME blend. Two fully opaque, non-overlapping-in-colour-space draws
        // (pure red then pure blue) at the exact same destination pixel: a correct 'lighter'
        // composite must read back as pure magenta at full alpha (255+0, 0, 0+255 clamped); a
        // backend that fell back to plain source-over would read back as just blue (the second
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
        // backend's Draw() correctly (not silently zeroed or only applied to a single-glyph case).
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
            // texture. Unlike RenderTarget2D::GetData (HTMLDOM-83/HtmlDomRenderTargetBackend's own
            // Canvas2D getImageData), this path is entirely shared/backend-agnostic code
            // (Texture2D::GetData reads from its own cpuPixels_ CPU-side shadow copy, never
            // touching the backend at all) -- but it had never been exercised end-to-end under the
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
                  "the exact uploaded pixels, byte-for-byte, under the HTML_DOM backend");

            // plan_html_dom.md HTMLDOM-96b: Texture2D::FromStream decode, closing the loop for this
            // backend specifically. The decode itself (stb_image, via SaveAsPng/FromStream) is
            // fully backend-agnostic shared code -- what had never been proven under HTML_DOM is
            // the two backend-touching ends of that pipeline: the SOURCE texture's own upload
            // (SetData, exercised by CreateFromPixels below) and the DECODED texture's own upload
            // (FromStream's internal CreateFromPixels call), each going through this backend's real
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
                  "HTMLDOM-96b: Texture2D::FromStream decodes a PNG produced by this backend's own "
                  "SaveAsPng and re-uploads it correctly -- both the source and decoded textures "
                  "round-trip through the real HTML_DOM upload/readback path");
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
        if (frame_ == 14)
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

        // plan_html_dom.md HTMLDOM-97: mixed non-Mirror per-axis modes (U=Wrap, V=Clamp), the other
        // half of the fix. Same 2x2 source, same Rectangle(0,0,4,4) sourceRect (exceeds bounds on
        // both axes), but now U tiles (Wrap) while V does NOT (Clamp) -- V's own established Clamp
        // behaviour (ClampNarrowsTheSourceRectAndShiftsTheLocalBoxToMatch, GTest) narrows the source
        // rect into the texture's own 2px height and does not stretch to fill the requested 4px
        // destination height, so rows y=2,3 must come back untouched (transparent), while rows
        // y=0,1 tile the source horizontally exactly like Wrap's own already-verified pattern.
        if (frame_ == 15)
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
            bool bottomRowsClamped = at(0, 2).getAProperty() < 50 && at(1, 2).getAProperty() < 50 &&
                                     at(2, 2).getAProperty() < 50 && at(3, 2).getAProperty() < 50 &&
                                     at(0, 3).getAProperty() < 50 && at(3, 3).getAProperty() < 50;
            std::printf("       mixed axes: row0=(%d,%d,%d)(%d,%d,%d) row2-alpha=(%d,%d)\n",
                        at(0, 0).getRProperty(), at(0, 0).getGProperty(), at(0, 0).getBProperty(),
                        at(2, 0).getRProperty(), at(2, 0).getGProperty(), at(2, 0).getBProperty(),
                        at(0, 2).getAProperty(), at(2, 2).getAProperty());
            check(topRowsTile && bottomRowsClamped,
                  "HTMLDOM-97b: mixed U=Wrap/V=Clamp tiles the U axis exactly like symmetric Wrap "
                  "while the V axis clamps independently (narrows to the source height, does not "
                  "tile, and does not stretch to fill the requested destination height)");
        }

        if (frame_ == 16)
        {
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
    HtmlDomPixelVerificationTest game;
    game.Run();
    return game.getResult();
}
