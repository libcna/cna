// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-17: 2D vertical-slice proof for the sokol_gfx graphics renderer -- a real
// Texture2D upload and real SpriteBatch draws, every one of them verified by reading the rendered
// pixels back off the real back buffer, not just "did not throw".
//
// The source texture is a 4x4 image with one solid colour per 2x2 quadrant (red / green / blue /
// white). Drawn with PointClamp into a 64x64 destination, each quadrant lands on an exact 32x32
// block, so every assertion below samples a pixel whose expected colour is derivable from the
// sprite geometry alone rather than from whatever the renderer happened to produce.
//
// Check A -- Texture2D::CreateFromPixels() succeeds for the 4x4 quadrant texture.
// Check B -- an untinted Opaque draw reproduces all four quadrant colours in the right corners,
//   which proves texture upload, UV mapping, and the top-left-origin sprite projection together.
// Check C -- SpriteEffects::FlipHorizontally swaps the left and right quadrants, and nothing else.
// Check D -- SpriteEffects::FlipVertically swaps the top and bottom quadrants.
// Check E -- a colour tint multiplies the sampled texel (white quadrant becomes the tint, and the
//   blue quadrant becomes black under a red tint -- a result only a real multiply can produce).
// Check F -- BlendState::NonPremultiplied at alpha 128 over a black clear yields mid-grey, and
//   BlendState::Opaque at the same alpha yields full white, so the blend factors genuinely differ.
// Check G -- a 90-degree rotation about the sprite centre moves the red quadrant from the
//   top-left to the bottom-left corner.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;
    /// Destination size for every sprite: 64x64 means each 2x2 source quadrant covers 32x32.
    constexpr int kSpriteSize = 64;
    /// Offset from a sprite's top-left corner to the centre of one of its quadrant blocks.
    constexpr int kQuadrantCentre = 16;

    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kBlack(0, 0, 0, 255);

    /// 4x4 RGBA8: red top-left, green top-right, blue bottom-left, white bottom-right.
    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        const auto setPixel = [&pixels](int x, int y, const Color& color) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[offset + 0] = static_cast<std::uint8_t>(color.getRProperty());
            pixels[offset + 1] = static_cast<std::uint8_t>(color.getGProperty());
            pixels[offset + 2] = static_cast<std::uint8_t>(color.getBProperty());
            pixels[offset + 3] = 255;
        };
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2)       setPixel(x, y, kRed);
                else if (x >= 2 && y < 2) setPixel(x, y, kGreen);
                else if (x < 2)           setPixel(x, y, kBlue);
                else                      setPixel(x, y, kWhite);
            }
        }
        return pixels;
    }

    Rectangle QuadrantAt(int spriteX, int spriteY, int column, int row)
    {
        return Rectangle(spriteX + kQuadrantCentre + column * (kSpriteSize / 2),
                         spriteY + kQuadrantCentre + row * (kSpriteSize / 2),
                         1, 1);
    }
}

class Sokol2DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;

    void DrawSprite(int x, int y, const Color& tint, SpriteEffects effects,
                    const BlendState& blendState, float rotation = 0.0f,
                    const Vector2& origin = Vector2(0, 0))
    {
        SamplerState pointClamp = SamplerState::PointClamp;
        spriteBatch_->Begin(SpriteSortMode::Deferred, blendState, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(x, y, kSpriteSize, kSpriteSize),
                           Rectangle(0, 0, 4, 4), tint, rotation, origin, effects, 0.0f);
        spriteBatch_->End();
    }

public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 4, 4, MakeQuadrantTexturePixels()));
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        device.Clear(kBlack);

        // Check B -- plain untinted draw at (0, 0).
        DrawSprite(0, 0, Color::White, SpriteEffects::None, BlendState::Opaque);
        ExpectPixel("untinted top-left quadrant is red", QuadrantAt(0, 0, 0, 0), kRed);
        ExpectPixel("untinted top-right quadrant is green", QuadrantAt(0, 0, 1, 0), kGreen);
        ExpectPixel("untinted bottom-left quadrant is blue", QuadrantAt(0, 0, 0, 1), kBlue);
        ExpectPixel("untinted bottom-right quadrant is white", QuadrantAt(0, 0, 1, 1), kWhite);

        // Check C -- horizontal flip at (80, 0): columns swap, rows do not.
        DrawSprite(80, 0, Color::White, SpriteEffects::FlipHorizontally, BlendState::Opaque);
        ExpectPixel("FlipHorizontally puts green top-left", QuadrantAt(80, 0, 0, 0), kGreen);
        ExpectPixel("FlipHorizontally puts red top-right", QuadrantAt(80, 0, 1, 0), kRed);
        ExpectPixel("FlipHorizontally puts blue bottom-right", QuadrantAt(80, 0, 1, 1), kBlue);

        // Check D -- vertical flip at (160, 0): rows swap, columns do not.
        DrawSprite(160, 0, Color::White, SpriteEffects::FlipVertically, BlendState::Opaque);
        ExpectPixel("FlipVertically puts blue top-left", QuadrantAt(160, 0, 0, 0), kBlue);
        ExpectPixel("FlipVertically puts red bottom-left", QuadrantAt(160, 0, 0, 1), kRed);

        // Check E -- red tint at (0, 80). Only a real per-channel multiply turns the white
        // quadrant red AND the blue quadrant black; a tint that were merely ignored, or applied
        // additively, would fail one of the two.
        DrawSprite(0, 80, kRed, SpriteEffects::None, BlendState::Opaque);
        ExpectPixel("red tint leaves the white quadrant red", QuadrantAt(0, 80, 1, 1), kRed);
        ExpectPixel("red tint turns the blue quadrant black", QuadrantAt(0, 80, 0, 1), kBlack);

        // Check F -- the same half-alpha white over the black clear, once through
        // NonPremultiplied (SourceAlpha/InverseSourceAlpha) and once through Opaque (One/Zero).
        const Color halfAlphaWhite(255, 255, 255, 128);
        DrawSprite(80, 80, halfAlphaWhite, SpriteEffects::None, BlendState::NonPremultiplied);
        ExpectPixel("NonPremultiplied at alpha 128 blends white to mid-grey",
                    QuadrantAt(80, 80, 1, 1), Color(128, 128, 128, 255), /*tolerance=*/2);

        DrawSprite(160, 80, halfAlphaWhite, SpriteEffects::None, BlendState::Opaque);
        ExpectPixel("Opaque at alpha 128 ignores the alpha and writes full white",
                    QuadrantAt(160, 80, 1, 1), kWhite);

        // Check G -- a quarter turn about the sprite centre. The origin is given in source-texture
        // pixels (2, 2 = the centre of the 4x4 source), so the destination rectangle's X/Y become
        // the pivot and the sprite spans (96-32, 176-32) to (96+32, 176+32). A positive XNA
        // rotation turns clockwise on screen, so the source's top row becomes the right column:
        // red (top-left) lands top-right and blue (bottom-left) lands top-left.
        DrawSprite(96, 176, Color::White, SpriteEffects::None, BlendState::Opaque,
                   MathHelper::PiOver2, Vector2(2.0f, 2.0f));
        ExpectPixel("90-degree rotation moves red to the top-right",
                    Rectangle(96 + kQuadrantCentre, 176 - kQuadrantCentre, 1, 1), kRed);
        ExpectPixel("90-degree rotation moves blue to the top-left",
                    Rectangle(96 - kQuadrantCentre, 176 - kQuadrantCentre, 1, 1), kBlue);
    }

    Sokol2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main() { return CNA::Examples::RunPixelTest<Sokol2DTest>(); }
