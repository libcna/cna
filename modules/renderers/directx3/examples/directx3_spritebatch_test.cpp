// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw path tests for the DIRECTX2
// (real DirectDraw v1, run under Wine -- no ../free-direct anywhere in this renderer) graphics renderer.
//
// Check A -- Draw() before Begin()/End() without Begin() throw (DX2-30 Begin/End contract).
// Check B -- Identity draw (BlendState::Opaque, 1:1 scale, no rotation/flip, white tint) is a real
//   BltFast straight copy -- exact pixel match (DX2-31).
// Check C -- General-path full-opacity draw under BlendState::AlphaBlend (NOT the Opaque preset,
//   so the identity fast path never triggers) reproduces the source color exactly -- proves the
//   CPU compositor itself, not just its blend math (DX2-32).
// Check D -- General-path zero-alpha draw under BlendState::NonPremultiplied (straight alpha)
//   leaves the destination exactly untouched -- proves the straight-alpha "over" baseline formula
//   (DX2-40's precursor).
// Check E -- SpriteEffects::FlipHorizontally swaps sampled texels left-right (DX2-34).
// Check F -- Scale (dest rect != source rect) samples the correct texel per screen region (DX2-35).
// Check G -- Rotation by pi around the texture's center origin maps the top-left source texel to
//   the bottom-right screen quadrant and vice versa (DX2-33).
// Check H -- SetTransformMatrix() (a translation) shifts an otherwise-identity draw by exactly the
//   translation offset (DX2-36).
// Check I -- SpriteSortMode is fully handled by shared SpriteBatch.cpp -- Begin(sortMode, ...)
//   with a non-Deferred mode draws without throwing or needing renderer-specific code (DX2-37).
// Check J -- Begin(..., a non-null custom Effect) throws: no programmable shader stage exists on
//   this renderer (DX2-38).
//
// Source-rectangle cropping (DX2-39) is exercised implicitly by every Draw() call above (all use
// an explicit sourceRectangle).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 96;
static constexpr float kPi = 3.14159265358979323846f;

class DirectX3SpriteBatchTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 10;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    static bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        // Check A: Begin/End contract.
        {
            Texture2D probeTex(dev, 1, 1);
            bool drawBeforeBeginThrew = false;
            try { sb.Draw(probeTex, 0.0f, 0.0f); }
            catch (const std::exception&) { drawBeforeBeginThrew = true; }
            bool endWithoutBeginThrew = false;
            try { sb.End(); }
            catch (const std::exception&) { endWithoutBeginThrew = true; }
            check(drawBeforeBeginThrew && endWithoutBeginThrew,
                  "Draw() before Begin() and End() without Begin() both throw");
        }

        dev.Clear(Color(10, 10, 10, 255));

        // Check B: identity fast path (BltFast), exact match.
        {
            Texture2D tex(dev, 4, 4);
            std::vector<Color> px(16, Color(200, 100, 50, 255));
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, 2.0f, 3.0f);
            sb.End();

            const Color got = ReadPixel(dev, 3, 4);
            check(SameColor(got, Color(200, 100, 50, 255)),
                  "Identity draw (BlendState::Opaque) is an exact BltFast copy");
        }

        // Check C: general-path full-opacity under AlphaBlend reproduces the source exactly.
        {
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px(4, Color(30, 200, 40, 255));
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
            sb.Draw(tex, Rectangle(20, 10, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            sb.End();

            const Color got = ReadPixel(dev, 20, 10);
            check(SameColor(got, Color(30, 200, 40, 255)),
                  "General-path full-opacity draw (AlphaBlend) reproduces the source exactly");
        }

        // Check D: general-path zero-alpha draw under BlendState::NonPremultiplied (straight
        // alpha) leaves the destination untouched. Real bug found and fixed during this port's
        // own first test run: this originally used BlendState::AlphaBlend, which in this codebase
        // (matching real XNA) is the PREMULTIPLIED preset -- out = src + dst*(1-srcAlpha), where
        // `src` is used as-is, NOT multiplied by srcAlpha again, because premultiplied convention
        // assumes the source RGB was already multiplied by alpha at authoring time. A
        // (255,0,0,0) texel is not validly premultiplied data (a truly transparent premultiplied
        // red pixel is (0,0,0,0)), so under AlphaBlend its full red channel legitimately bleeds
        // through regardless of alpha -- correct XNA behavior (see BlendState::NonPremultiplied's
        // own doc), not a compositor bug. Switched to NonPremultiplied (out =
        // src*srcAlpha + dst*(1-srcAlpha)), which is the preset a straight-alpha, zero-alpha
        // source pixel actually needs to leave the destination untouched.
        {
            dev.Clear(Color(5, 6, 7, 255));
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px(4, Color(255, 0, 0, 0));
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
            sb.Draw(tex, Rectangle(30, 10, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            sb.End();

            const Color got = ReadPixel(dev, 30, 10);
            check(SameColor(got, Color(5, 6, 7, 255)),
                  "General-path zero-alpha draw (NonPremultiplied) leaves the destination untouched");
        }

        dev.Clear(Color(1, 1, 1, 255));

        // Check E: SpriteEffects::FlipHorizontally swaps sampled texels.
        {
            Texture2D tex(dev, 2, 1);
            std::vector<Color> px = { Color(255, 0, 0, 255), Color(0, 255, 0, 255) };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(40, 20, 2, 1), Rectangle(0, 0, 2, 1), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipHorizontally, 0.0f);
            sb.End();

            const bool ok = SameColor(ReadPixel(dev, 40, 20), Color(0, 255, 0, 255)) &&
                            SameColor(ReadPixel(dev, 41, 20), Color(255, 0, 0, 255));
            check(ok, "SpriteEffects::FlipHorizontally swaps sampled texels left-right");
        }

        // Check F: scale samples the correct texel per screen region.
        {
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px = { Color(255, 0, 0, 255), Color(0, 255, 0, 255),   // row0: (0,0) (1,0)
                                     Color(0, 0, 255, 255), Color(255, 255, 0, 255) }; // row1: (0,1) (1,1)
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(10, 40, 8, 8), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            sb.End();

            const bool ok = SameColor(ReadPixel(dev, 11, 41), Color(255, 0, 0, 255)) &&      // top-left quadrant
                            SameColor(ReadPixel(dev, 16, 46), Color(255, 255, 0, 255));      // bottom-right quadrant
            check(ok, "Scale (2x2 texture -> 8x8 dest) samples the correct texel per quadrant");
        }

        // Check G: rotation by pi around the texture's center origin.
        {
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px = { Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                     Color(0, 0, 255, 255), Color(255, 255, 0, 255) };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(60, 40, 8, 8), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255),
                    kPi, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
            sb.End();

            // origin=(1,1) is the texture's center, so the drawn quad is centered ON (60,40), not
            // offset from it: screen bounding box is [56,64]x[36,44]. Un-rotated, texel(0,0)=Red
            // would land in the top-left quadrant and texel(1,1)=Yellow in the bottom-right one.
            // After a pi rotation about that same center, the quadrants swap.
            // Sample points are offset (+1,+1)/(-1,-1) from each respective bbox corner (56,36)/
            // (64,44), the same "1px in from the corner" convention Check F already uses (11,41
            // relative to its own (10,40) corner) -- with only a 2x2 source texture and the
            // default bilinear (Linear) sampler, only the pixels immediately adjacent to a corner
            // (where Clamp addressing saturates all 4 bilinear taps to the same texel) read back a
            // pure, unblended color; a real bug was found and fixed here during this port's own
            // first test run -- the original sample point (58,38) was 2px in from the corner,
            // already inside the blend gradient (confirmed empirically: it read (199,223,27), not
            // pure yellow) -- not a compositor/rotation-math bug, a test sample-point precision bug.
            const bool ok = SameColor(ReadPixel(dev, 62, 42), Color(255, 0, 0, 255)) &&    // bottom-right quadrant
                            SameColor(ReadPixel(dev, 57, 37), Color(255, 255, 0, 255));    // top-left quadrant
            check(ok, "Rotation by pi around the center origin maps top-left <-> bottom-right");
        }

        // Check H: SetTransformMatrix() shifts an otherwise-identity draw.
        {
            dev.Clear(Color(9, 9, 9, 255));
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px(4, Color(11, 22, 33, 255));
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, nullptr,
                     Matrix::CreateTranslation(5.0f, 7.0f, 0.0f));
            sb.Draw(tex, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            sb.End();

            const bool untranslatedIsClearColor = SameColor(ReadPixel(dev, 0, 0), Color(9, 9, 9, 255));
            const bool translatedHasTexture = SameColor(ReadPixel(dev, 5, 7), Color(11, 22, 33, 255));
            check(untranslatedIsClearColor && translatedHasTexture,
                  "SetTransformMatrix() (translation) shifts the draw by exactly the offset");
        }

        // Check I: SpriteSortMode is fully handled by shared SpriteBatch.cpp -- no renderer-specific
        // code needed; a non-Deferred mode must draw without throwing.
        {
            bool threw = false;
            try
            {
                Texture2D tex(dev, 1, 1);
                std::vector<Color> px(1, Color(1, 2, 3, 255));
                tex.SetData(px.data(), 1);
                sb.Begin(SpriteSortMode::BackToFront, BlendState::Opaque);
                sb.Draw(tex, 0.0f, 0.0f);
                sb.End();
            }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("SpriteSortMode::BackToFront draw threw: %s\n", e.what());
            }
            check(!threw, "SpriteSortMode::BackToFront draws without throwing (fully shared-code handled)");
        }

        // Check J: custom Effect throws.
        {
            bool threw = false;
            try
            {
                BasicEffect effect(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr, &effect);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw, "Begin() with a non-null custom Effect throws (no shader stage on DIRECTX3)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    DirectX3SpriteBatchTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX3SpriteBatchTest game;
    game.Run();
    return game.getResult();
}
