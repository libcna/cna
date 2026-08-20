// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString tests for the DIRECTX5 (real
// DirectDraw v1, run under Wine -- no ../free-direct anywhere in this renderer) graphics renderer.
//
// SpriteBatch::DrawString (shared, renderer-agnostic SpriteBatch.cpp) lays out each glyph as a
// destination rectangle and calls pushSprite() -> renderer_->Draw(), the EXACT same
// ISpriteBatchRenderer::Draw() overload Phase O4/O5 already implemented and pixel-verified for
// plain sprites. This phase therefore needs no new renderer code (DX2-50/51 confirm this
// directly); it exists to prove that claim empirically rather than merely assert it.
//
// A 2-glyph atlas is used throughout: glyph 'A' = solid Red 8x8 at atlas (0,0,8,8), glyph 'B' =
// solid Green 8x8 at atlas (8,0,8,8). Unlike the existing SDL_Renderer SpriteFont tests (which use
// a color-match tolerance to work around that renderer's own physical/logical scaling), DIRECTX5's CPU
// compositor is exact-pixel, so every check here asserts an exact color match.
//
// Check A (DX2-50) -- a single glyph lands at exactly the expected destination rect.
// Check B (DX2-51) -- two glyphs with spacing land at their correctly kerning-advanced positions,
//   with the gap between them staying background (proves spacing/kerning, not just glyph 1).
// Check C (DX2-52) -- '\n' advances to the next line by exactly lineSpacing pixels, resetting X.
// Check D (DX2-53) -- an unresolvable character falls back to `defaultCharacter` when set, and
//   throws std::invalid_argument when not set.
// Check E (DX2-54) -- SpriteEffects::FlipHorizontally with DrawString mirrors the WHOLE glyph
//   sequence (B then A, not just each glyph individually), the shared Task 694 fix.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 64;
static const Color kRed(255, 0, 0, 255);
static const Color kGreen(0, 255, 0, 255);
static const Color kBlack(0, 0, 0, 255);

class DirectX5SpriteFontTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 5;
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

    // Builds a 16x8 atlas: 'A' = Red at (0,0,8,8), 'B' = Green at (8,0,8,8). Zero cropping offset,
    // zero left/right kerning bearing (kerning.Y = glyph width, matching the reference SDL_Renderer
    // fixture's convention) so destination rects map directly and predictably.
    static std::unique_ptr<SpriteFont> BuildFont(GraphicsDevice& dev, Texture2D& atlas,
                                                 int lineSpacing, float spacing,
                                                 std::optional<SharpRuntime::charcs> defaultChar)
    {
        atlas = Texture2D(dev, 16, 8);
        std::vector<Color> pixels(128, kBlack);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                pixels[static_cast<std::size_t>(y) * 16 + x] = (x < 8) ? kRed : kGreen;
        atlas.SetData(pixels.data(), static_cast<int>(pixels.size()));

        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'A', u'B' };
        std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f) };
        return std::make_unique<SpriteFont>(atlas, glyphBounds, cropping, characters,
                                            lineSpacing, spacing, kerning, defaultChar);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);
        Texture2D atlas;

        // Check A (DX2-50): single glyph lands at exactly the expected destination rect.
        {
            auto font = BuildFont(dev, atlas, 8, 2.0f, std::nullopt);
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb.Begin();
            sb.DrawString(*font, "A", Vector2(4.0f, 4.0f), Color::White);
            sb.End();
            const bool ok = SameColor(ReadPixel(dev, 8, 8), kRed) &&
                            SameColor(ReadPixel(dev, 2, 2), kBlack) &&
                            SameColor(ReadPixel(dev, 13, 13), kBlack);
            check(ok, "Single glyph 'A' lands at exactly (4,4,8,8) (DX2-50)");
        }

        // Check B (DX2-51): two glyphs with spacing -- 'A' at x=4, 'B' at x=4+8+spacing=14, gap
        // between them (x=12,13) stays background.
        {
            auto font = BuildFont(dev, atlas, 8, 2.0f, std::nullopt);
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb.Begin();
            sb.DrawString(*font, "AB", Vector2(4.0f, 4.0f), Color::White);
            sb.End();
            const bool ok = SameColor(ReadPixel(dev, 8, 8), kRed) &&
                            SameColor(ReadPixel(dev, 18, 8), kGreen) &&
                            SameColor(ReadPixel(dev, 12, 8), kBlack);
            check(ok, "Two glyphs 'AB' with spacing land correctly, gap stays background (DX2-51)");
        }

        // Check C (DX2-52): '\n' advances to the next line by exactly lineSpacing (8) pixels.
        {
            auto font = BuildFont(dev, atlas, 8, 2.0f, std::nullopt);
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb.Begin();
            sb.DrawString(*font, "A\nB", Vector2(4.0f, 4.0f), Color::White);
            sb.End();
            const bool ok = SameColor(ReadPixel(dev, 8, 8), kRed) &&      // line 1: 'A' at y=4
                            SameColor(ReadPixel(dev, 8, 16), kGreen);     // line 2: 'B' at y=4+8
            check(ok, "'\\n' advances to the next line by exactly lineSpacing (DX2-52)");
        }

        // Check D (DX2-53): unresolvable character falls back to defaultCharacter when set, and
        // throws when not set.
        {
            auto fontWithFallback = BuildFont(dev, atlas, 8, 2.0f, std::optional<SharpRuntime::charcs>(u'A'));
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb.Begin();
            sb.DrawString(*fontWithFallback, "Z", Vector2(4.0f, 4.0f), Color::White);
            sb.End();
            const bool fallbackOk = SameColor(ReadPixel(dev, 8, 8), kRed);

            auto fontNoFallback = BuildFont(dev, atlas, 8, 2.0f, std::nullopt);
            bool threw = false;
            try
            {
                sb.Begin();
                sb.DrawString(*fontNoFallback, "Z", Vector2(4.0f, 4.0f), Color::White);
                sb.End();
            }
            catch (const std::invalid_argument&)
            {
                threw = true;
                // The throw happens mid-DrawString, before End() -- close out the batch cleanly
                // so it doesn't leak into the next check's Begin().
                sb.End();
            }
            check(fallbackOk && threw,
                  "Unresolvable char uses defaultCharacter when set, throws when not (DX2-53)");
        }

        // Check E (DX2-54): SpriteEffects::FlipHorizontally mirrors the whole glyph sequence
        // (Task 694's shared-code fix) -- 'B' (green) ends up on the LEFT, 'A' (red) on the RIGHT.
        {
            auto font = BuildFont(dev, atlas, 8, 2.0f, std::nullopt);
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb.Begin();
            sb.DrawString(*font, "AB", Vector2(4.0f, 4.0f), Color::White, 0.0f, Vector2::Zero,
                         Vector2(1.0f, 1.0f), SpriteEffects::FlipHorizontally, 0.0f);
            sb.End();
            const bool ok = SameColor(ReadPixel(dev, 8, 8), kGreen) &&
                            SameColor(ReadPixel(dev, 18, 8), kRed);
            check(ok, "SpriteEffects::FlipHorizontally mirrors the whole glyph sequence (DX2-54)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    DirectX5SpriteFontTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX5SpriteFontTest game;
    game.Run();
    return game.getResult();
}
