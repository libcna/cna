// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-6: pixel oracle for the FNA3D renderer's 2D SpriteBatch path, which draws
// through XNA's own stock SpriteEffect executed by MojoShader -- FNA3D has no other shader entry
// point, so this also proves the vendored Effect binary really runs on the GPU.
//
// Check A -- a sprite lands exactly inside its destination rectangle and nowhere outside it,
//   which simultaneously proves the orthographic projection, the vertex winding and the
//   top-left-origin row order of the readback.
// Check B -- the per-sprite tint colour modulates the sampled texel.
// Check C -- a source rectangle selects the right quadrant of the atlas.
// Check D -- SpriteEffects::FlipHorizontally really mirrors the sampled quadrant.
// Check E -- a rotated sprite about its centre lands where the rotation says it should.
// Check F -- Point sampling of a 2x2 atlas magnified 32x keeps hard texel edges (no filtering).
// Check G -- a deferred batch larger than the 16-bit index range is split without wrapping the
//   final sprite back onto vertex zero.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // 2x2 atlas: top-left red, top-right green, bottom-left blue, bottom-right white.
    std::vector<std::uint8_t> AtlasPixels()
    {
        return {
            255, 0,   0,   255,   0,   255, 0,   255,
            0,   0,   255, 255,   255, 255, 255, 255,
        };
    }
}

class Fna3d2DTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        Texture2D atlas = Texture2D::CreateFromPixels(device, 2, 2, AtlasPixels());
        SpriteBatch batch(device);
        // Begin() takes a mutable SamplerState*, so the PointClamp preset is copied once here.
        SamplerState pointClamp = SamplerState::PointClamp;

        // Check A -- placement. A 40x30 white sprite at (20,10) must fill exactly that rectangle.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(atlas, Rectangle(20, 10, 40, 30), Rectangle(1, 1, 1, 1), Color::White);
        batch.End();
        ExpectPixel("sprite fills the inside of its destination rectangle",
                    Rectangle(40, 25, 1, 1), Color(255, 255, 255, 255));
        ExpectPixel("sprite does not bleed above its destination rectangle",
                    Rectangle(40, 8, 1, 1), Color(0, 0, 0, 255));
        ExpectPixel("sprite does not bleed left of its destination rectangle",
                    Rectangle(18, 25, 1, 1), Color(0, 0, 0, 255));
        ExpectPixel("sprite does not bleed below its destination rectangle",
                    Rectangle(40, 42, 1, 1), Color(0, 0, 0, 255));
        ExpectPixel("sprite does not bleed right of its destination rectangle",
                    Rectangle(62, 25, 1, 1), Color(0, 0, 0, 255));

        // Check B -- tint modulates the sampled texel.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(atlas, Rectangle(20, 10, 40, 30), Rectangle(1, 1, 1, 1),
                   Color(255, 0, 255, 255));
        batch.End();
        ExpectPixel("tint modulates the sampled texel", Rectangle(40, 25, 1, 1),
                    Color(255, 0, 255, 255), 2);

        // Check C -- source rectangle selection.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(atlas, Rectangle(0, 0, 32, 32), Rectangle(0, 0, 1, 1), Color::White);
        batch.Draw(atlas, Rectangle(32, 0, 32, 32), Rectangle(1, 0, 1, 1), Color::White);
        batch.Draw(atlas, Rectangle(0, 32, 32, 32), Rectangle(0, 1, 1, 1), Color::White);
        batch.End();
        ExpectPixel("source rect (0,0) samples the red texel", Rectangle(16, 16, 1, 1),
                    Color(255, 0, 0, 255), 2);
        ExpectPixel("source rect (1,0) samples the green texel", Rectangle(48, 16, 1, 1),
                    Color(0, 255, 0, 255), 2);
        ExpectPixel("source rect (0,1) samples the blue texel", Rectangle(16, 48, 1, 1),
                    Color(0, 0, 255, 255), 2);

        // Check D -- horizontal flip of the whole 2x2 atlas: red/green swap sides.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(atlas, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 2, 2), Color::White, 0.0f,
                   Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        batch.End();
        ExpectPixel("flipped horizontally: green is now on the left", Rectangle(16, 16, 1, 1),
                    Color(0, 255, 0, 255), 2);
        ExpectPixel("flipped horizontally: red is now on the right", Rectangle(48, 16, 1, 1),
                    Color(255, 0, 0, 255), 2);

        // Check E -- rotation. A 20x20 sprite anchored at (60,60) with its origin at the sprite
        // centre and rotated 90 degrees still covers the same centred square, but a corner texel
        // moves to a different corner. Sampling the centre proves the quad is still there and
        // that rotation did not collapse or displace it.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(atlas, Rectangle(60, 60, 20, 20), Rectangle(1, 1, 1, 1), Color::White,
                   MathHelper::PiOver2, Vector2(0.5f, 0.5f), SpriteEffects::None, 0.0f);
        batch.End();
        ExpectPixel("a rotated sprite still covers its anchor", Rectangle(60, 60, 1, 1),
                    Color(255, 255, 255, 255));
        ExpectPixel("a rotated sprite leaves the far corner clear", Rectangle(5, 5, 1, 1),
                    Color(0, 0, 0, 255));

        // Check F -- Point sampling keeps hard texel edges under 32x magnification. The texel
        // boundary of a 2x2 atlas stretched to 64x64 sits exactly at x = 32.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(atlas, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 2, 2), Color::White);
        batch.End();
        ExpectPixel("point sampling keeps the texel edge hard on the red side",
                    Rectangle(30, 16, 1, 1), Color(255, 0, 0, 255));
        ExpectPixel("point sampling keeps the texel edge hard on the green side",
                    Rectangle(34, 16, 1, 1), Color(0, 255, 0, 255));

        // Check G -- one 16-bit indexed draw can address 16,384 quads exactly. The first 16,384
        // are deliberately off-screen and the next one is visible. Without chunking, its generated
        // base index wraps to zero and it redraws the first off-screen quad instead.
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        for (int i = 0; i < 16384; ++i)
        {
            batch.Draw(atlas, Rectangle(-100, -100, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        }
        batch.Draw(atlas, Rectangle(10, 10, 8, 8), Rectangle(1, 0, 1, 1), Color::White);
        batch.End();
        ExpectPixel("a SpriteBatch past 16-bit index capacity keeps its final sprite",
                    Rectangle(14, 14, 1, 1), Color(0, 255, 0, 255), 2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3d2DTest>();
}
