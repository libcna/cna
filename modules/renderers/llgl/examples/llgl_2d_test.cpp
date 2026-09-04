// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-10..LLGL-16: 2D vertical-slice proof for the LLGL graphics renderer, asserted
// against real pixels read back from the GPU rather than against "nothing threw".
//
// The texture is a 4x4 image of four solid 2x2 quadrants (red / green / blue / white), drawn
// magnified with a Point sampler so each quadrant covers a large, unambiguous block of the back
// buffer. Sampling the centre of each block therefore proves the upload, the texture coordinates,
// the orientation (top-left texel lands in the top-left corner, which is where a Y-flip mistake
// shows up immediately), the sampler, and the blend state.
//
// Check A -- Texture2D::CreateFromPixels() succeeds for the 4x4 image.
// Check B -- the cleared back buffer really is the clear colour.
// Check C..F -- an unrotated, untinted, Point-sampled draw puts each source quadrant in the right
//   corner of the destination rectangle.
// Check G -- a half-alpha draw over a known background blends towards it instead of overwriting.
// Check H -- a red-tinted draw multiplies the sampled texel by the tint.
// Check I -- SpriteEffects::FlipHorizontally really mirrors the sprite's X axis.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
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
    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        // 4x4 RGBA8: one solid colour per 2x2 quadrant -- red / green / blue / white.
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = a;
        };
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2)       setPixel(x, y, 255, 0, 0, 255);
                else if (x >= 2 && y < 2) setPixel(x, y, 0, 255, 0, 255);
                else if (x < 2)           setPixel(x, y, 0, 0, 255, 255);
                else                      setPixel(x, y, 255, 255, 255, 255);
            }
        }
        return pixels;
    }
}

class Llgl2DTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<Texture2D> texture;
        try
        {
            texture = std::make_unique<Texture2D>(
                Texture2D::CreateFromPixels(device, 4, 4, MakeQuadrantTexturePixels()));
        }
        catch (const std::exception&)
        {
            texture.reset();
        }
        ExpectTrue("Texture2D::CreateFromPixels() succeeds for a 4x4 texture",
                   texture != nullptr && texture->getWidthProperty() == 4 &&
                   texture->getHeightProperty() == 4);
        if (texture == nullptr)
            return;

        device.Clear(Color(0, 0, 0, 255));

        SpriteBatch spriteBatch(device);
        SamplerState pointClamp = SamplerState::PointClamp;

        // 80x80 at (16,16): each source quadrant covers a 40x40 block of the destination.
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*texture, Rectangle(16, 16, 80, 80), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch.End();

        // Half-alpha white over the black clear, with NonPremultiplied (SourceAlpha /
        // InverseSourceAlpha). AlphaBlend would be wrong here and would prove nothing: it is XNA's
        // PREmultiplied preset (One / InverseSourceAlpha), under which a non-premultiplied
        // half-alpha white legitimately comes out fully white. Landing near mid grey is what tells
        // "the blend state reached the pipeline" apart from both "overwrote the background" and
        // "never drew" -- and it exercises a second entry in the pipeline cache.
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*texture, Rectangle(120, 16, 40, 40), Rectangle(2, 2, 2, 2),
                         Color(255, 255, 255, 128));
        spriteBatch.End();

        // Red tint over the white quadrant: the shader multiplies texel by tint, so only the red
        // channel survives.
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*texture, Rectangle(180, 16, 40, 40), Rectangle(2, 2, 2, 2),
                         Color(255, 0, 0, 255));
        spriteBatch.End();

        // Horizontally flipped: the red top-left quadrant has to end up on the RIGHT.
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*texture, Rectangle(16, 120, 80, 80), Rectangle(0, 0, 4, 4), Color::White,
                         0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        spriteBatch.End();

        ExpectPixel("cleared background stays the clear colour", Rectangle(280, 200, 1, 1),
                    Color(0, 0, 0, 255));

        ExpectPixel("top-left source quadrant lands top-left (red)", Rectangle(36, 36, 1, 1),
                    Color(255, 0, 0, 255));
        ExpectPixel("top-right source quadrant lands top-right (green)", Rectangle(76, 36, 1, 1),
                    Color(0, 255, 0, 255));
        ExpectPixel("bottom-left source quadrant lands bottom-left (blue)", Rectangle(36, 76, 1, 1),
                    Color(0, 0, 255, 255));
        ExpectPixel("bottom-right source quadrant lands bottom-right (white)", Rectangle(76, 76, 1, 1),
                    Color(255, 255, 255, 255));

        ExpectPixel("half-alpha white over black blends to mid grey (NonPremultiplied)", Rectangle(140, 36, 1, 1),
                    Color(128, 128, 128, 255), /*tolerance=*/4);

        ExpectPixel("red tint multiplies the sampled white texel", Rectangle(200, 36, 1, 1),
                    Color(255, 0, 0, 255));

        ExpectPixel("FlipHorizontally puts the red quadrant on the right", Rectangle(76, 140, 1, 1),
                    Color(255, 0, 0, 255));
        ExpectPixel("FlipHorizontally puts the green quadrant on the left", Rectangle(36, 140, 1, 1),
                    Color(0, 255, 0, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Llgl2DTest>();
}
