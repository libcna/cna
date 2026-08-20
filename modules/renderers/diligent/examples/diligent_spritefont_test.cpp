// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-53: real-device SpriteFont glyph placement/spacing/newline/flip pixel
// test, mirroring D3D11's own already-closed DX-127 (examples/directx11_smoke_test.cpp) and D3D12's
// DX-132/DX-148, and EasyGL's established fixture (Tasks 424-429). Shared, renderer-agnostic C++
// (SpriteFont/SpriteBatch) has been pixel-verified on EasyGL, D3D11 and D3D12 already, per
// docs/graphics-renderer-feature-matrix.md, but no Diligent equivalent existed before this task.
//
// An 8x8 solid-white atlas per glyph, zero cropping offset, zero left/right kerning bearing, so a
// glyph's destination rect maps exactly and any placement error is a hard pixel difference.
//
// Check A -- single glyph at (4,4) occupies exactly [4,12) x [4,12): checked inside plus all four
//   edge midpoints, so an X-only or Y-only misplacement cannot pass.
// Check B -- "AB" advances the SECOND glyph by exactly one glyph width (8px); both cells drawn and
//   nothing spills past them.
// Check C -- "A\nA" drops the second line by exactly lineSpacing AND resets x to the start -- a
//   newline that only did one of the two cannot pass.
// Check D -- SpriteEffects::FlipVertically genuinely flips an ASYMMETRIC (top-half-white) glyph:
//   unflipped it lands top-half-white, flipped it lands bottom-half-white, so a no-op flip cannot
//   pass.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DiligentSpriteFontTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D atlas(device, 16, 8); // two 8x8 glyph cells side by side
        std::vector<Color> atlasPx(16 * 8, Color(255, 255, 255, 255));
        atlas.SetData(atlasPx.data(), 16 * 8);

        std::vector<Rectangle> glyphBounds = {Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8)};
        std::vector<Rectangle> cropping = {Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8)};
        std::vector<SharpRuntime::charcs> chars = {u'A', u'B'};
        std::vector<Vector3> kerning = {Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f)};
        SpriteFont font(atlas, glyphBounds, cropping, chars,
                        /*lineSpacing=*/8, /*spacing=*/0.0f, kerning,
                        std::optional<SharpRuntime::charcs>(std::nullopt));

        SpriteBatch fontBatch(device);

        auto readPixel = [&](int x, int y) -> Color {
            const Rectangle region(x, y, 1, 1);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&region, &pixel, 0, 1);
            return pixel;
        };
        auto isWhite = [](const Color& c) {
            return c.getRProperty() > 200 && c.getGProperty() > 200 && c.getBProperty() > 200;
        };
        auto isBlack = [](const Color& c) {
            return c.getRProperty() < 60 && c.getGProperty() < 60 && c.getBProperty() < 60;
        };

        // (A) Single glyph at (4,4) -> occupies exactly [4,12) x [4,12).
        device.Clear(Color(0, 0, 0, 255));
        fontBatch.Begin();
        fontBatch.DrawString(font, "A", Vector2(4.0f, 4.0f), Color::White);
        fontBatch.End();
        const bool glyphPlaced = isWhite(readPixel(8, 8))    // inside
            && isBlack(readPixel(3, 8))    // just left of the left edge
            && isBlack(readPixel(12, 8))   // just right of the right edge
            && isBlack(readPixel(8, 3))    // just above the top edge
            && isBlack(readPixel(8, 12));  // just below the bottom edge
        Check(glyphPlaced,
              "SpriteBatch::DrawString(): places a single glyph at EXACTLY its destination rect "
              "(4,4,8,8) -- checked inside plus all four edge midpoints");

        // (B) Two glyphs -> the second must advance by exactly one glyph width (8px).
        device.Clear(Color(0, 0, 0, 255));
        fontBatch.Begin();
        fontBatch.DrawString(font, "AB", Vector2(0.0f, 0.0f), Color::White);
        fontBatch.End();
        const bool spacingOk = isWhite(readPixel(4, 4))    // glyph 'A' cell [0,8)
            && isWhite(readPixel(12, 4))   // glyph 'B' cell [8,16) -- advanced by 8
            && isBlack(readPixel(20, 4));  // nothing beyond the second glyph
        Check(spacingOk,
              "SpriteBatch::DrawString(\"AB\"): advances the SECOND glyph by exactly one glyph "
              "width -- both cells are drawn and nothing spills past them");

        // (C) Newline -> the second line must drop by exactly lineSpacing (8px), back to x=0.
        device.Clear(Color(0, 0, 0, 255));
        fontBatch.Begin();
        fontBatch.DrawString(font, "A\nA", Vector2(0.0f, 0.0f), Color::White);
        fontBatch.End();
        const bool newlineOk = isWhite(readPixel(4, 4))    // line 1, y in [0,8)
            && isWhite(readPixel(4, 12))   // line 2, y in [8,16) -- dropped by lineSpacing
            && isBlack(readPixel(12, 4));  // line 1 has ONE glyph -- x reset, not advanced
        Check(newlineOk,
              "SpriteBatch::DrawString(\"A\\nA\"): drops the second line by exactly lineSpacing "
              "AND resets x to the start");

        // (D) SpriteEffects::FlipVertically -- an ASYMMETRIC glyph is required. Atlas glyph 'A' is
        // white only in its TOP half; flipped, the white must land in the BOTTOM half.
        std::vector<Color> halfPx(16 * 8, Color(0, 0, 0, 255));
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
                halfPx[static_cast<std::size_t>(y) * 16 + x] = Color(255, 255, 255, 255);
        Texture2D atlasHalf(device, 16, 8);
        atlasHalf.SetData(halfPx.data(), 16 * 8);
        SpriteFont fontHalf(atlasHalf, glyphBounds, cropping, chars, 8, 0.0f, kerning,
                            std::optional<SharpRuntime::charcs>(std::nullopt));

        device.Clear(Color(0, 0, 0, 255));
        fontBatch.Begin();
        fontBatch.DrawString(fontHalf, "A", Vector2(0.0f, 0.0f), Color::White, 0.0f,
                             Vector2(0.0f, 0.0f), 1.0f, SpriteEffects::None, 0.0f);
        fontBatch.End();
        const bool noFlipOk = isWhite(readPixel(4, 2))   // top half white
            && isBlack(readPixel(4, 6));  // bottom half black

        device.Clear(Color(0, 0, 0, 255));
        fontBatch.Begin();
        fontBatch.DrawString(fontHalf, "A", Vector2(0.0f, 0.0f), Color::White, 0.0f,
                             Vector2(0.0f, 0.0f), 1.0f, SpriteEffects::FlipVertically, 0.0f);
        fontBatch.End();
        const bool flipOk = isBlack(readPixel(4, 2))     // top half now black
            && isWhite(readPixel(4, 6));    // bottom half now white -- genuinely flipped

        Check(noFlipOk && flipOk,
              "SpriteBatch::DrawString(): SpriteEffects::FlipVertically genuinely flips a glyph -- "
              "an asymmetric (top-half-white) glyph lands top-half-white unflipped and "
              "bottom-half-white flipped");

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = passCount_ == 4 ? 0 : 1;
        Exit();
    }

public:
    DiligentSpriteFontTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentSpriteFontTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
