// SPDX-License-Identifier: MS-PL
// SpriteBatch SamplerState regression test for the PortableGL renderer.
//
// SpriteBatch resolves a null SamplerState to SamplerState.LinearClamp and forwards the resolved
// filter and address modes to the renderer. PortableGL overrode neither SetSamplerFilter nor
// SetSamplerAddressMode, while its texture creation hard-coded GL_NEAREST and GL_CLAMP_TO_EDGE --
// so the default batch sampled Point instead of Linear and no caller-selected mode had any effect.
//
// The textures below are built so the three address modes CANNOT coincide: a 2x1-column texture
// whose left column is red and right column is green, sampled across two extra tiles, reads
// (red, green) under Wrap, (green, green) under Clamp and (green, red) under Mirror.
//
// Check A -- the DEFAULT SpriteBatch batch really samples Linear, not Point.
// Check B -- SamplerState.PointClamp samples Point: a hard texel edge, no interpolated values.
// Check C -- SamplerState.LinearClamp samples Linear: interpolated values at the same texel edge.
// Check D -- Clamp, Wrap and Mirror each produce their own distinct tile pattern.
// Check E -- a sampler PortableGL cannot represent is refused deterministically, and the next
//   valid batch still draws.
// Check F -- switching sampler state between batches takes effect each time (PointWrap ->
//   LinearClamp -> PointWrap).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 64;
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
}

class PortableGLSamplerTest : public PortableGLTestGame
{
    std::unique_ptr<SpriteBatch> spriteBatch_;
    /// 2x2, left column black, right column white -- a hard vertical edge for the filter checks.
    std::unique_ptr<Texture2D> edge_;
    /// 2x2, left column red, right column green -- distinct colours for the address-mode checks.
    std::unique_ptr<Texture2D> columns_;

    /// Draws @p texture over the whole backbuffer, sampling @p source texels.
    void DrawSprite(Texture2D& texture, const Rectangle& source, SamplerState* sampler)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler,
                            nullptr, nullptr);
        spriteBatch_->Draw(texture, Rectangle(0, 0, kSize, kSize),
                           std::optional<Rectangle>(source), Color::White);
        spriteBatch_->End();
    }

    /// The colour at the centre of tile @p index of four equal vertical tiles.
    Color TileColour(int index)
    {
        return PixelAt(index * (kSize / 4) + (kSize / 8), kSize / 2);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<std::uint8_t> edgePixels = {
            0, 0, 0, 255,   255, 255, 255, 255,
            0, 0, 0, 255,   255, 255, 255, 255,
        };
        edge_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 2, 2, edgePixels));

        const std::vector<std::uint8_t> columnPixels = {
            255, 0, 0, 255,   0, 255, 0, 255,
            255, 0, 0, 255,   0, 255, 0, 255,
        };
        columns_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 2, 2, columnPixels));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Rectangle wholeTexture(0, 0, 2, 2);
        // Two texture widths across the destination, so tiles 2 and 3 are outside [0,1) in U and
        // the address mode decides what they sample.
        const Rectangle doubleWide(0, 0, 4, 2);

        // Check A -- the default batch. SpriteBatch resolves a null SamplerState to LinearClamp,
        // so the texel edge must be INTERPOLATED: the pixels either side of x = 32 are neither
        // pure black nor pure white. Point sampling gives exactly 0 and exactly 255 there.
        {
            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*edge_, wholeTexture, nullptr);
            const Color left = PixelAt(31, 32);
            const Color right = PixelAt(32, 32);
            check(left.getRProperty() > 0 && left.getRProperty() < 255 &&
                      right.getRProperty() > 0 && right.getRProperty() < 255,
                  "check A: the default SpriteBatch batch samples LinearClamp, not Point");
        }

        // Check B -- Point, explicitly. Now the same two pixels straddle a hard edge.
        {
            dev.Clear(Color(0, 0, 255, 255));
            SamplerState point = SamplerState::PointClamp;
            DrawSprite(*edge_, wholeTexture, &point);
            check(PixelAt(31, 32).getRProperty() == 0 && PixelAt(32, 32).getRProperty() == 255,
                  "check B: SamplerState.PointClamp samples the nearest texel, with no blend");
        }

        // Check C -- Linear, explicitly, for the same geometry.
        {
            dev.Clear(Color(0, 0, 255, 255));
            SamplerState linear = SamplerState::LinearClamp;
            DrawSprite(*edge_, wholeTexture, &linear);
            const Color left = PixelAt(31, 32);
            const Color right = PixelAt(32, 32);
            check(left.getRProperty() > 0 && left.getRProperty() < 255 &&
                      right.getRProperty() > left.getRProperty(),
                  "check C: SamplerState.LinearClamp interpolates across the same texel edge");
        }

        // Check D -- the three address modes, each with its own signature over tiles 2 and 3.
        // Point filtering keeps every tile a flat texel colour, so the readback is exact.
        {
            SamplerState clamp = SamplerState::PointClamp;
            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*columns_, doubleWide, &clamp);
            check(TileColour(0).getRProperty() == 255 && TileColour(1).getGProperty() == 255,
                  "check D: the in-range tiles sample their own texels under every address mode");
            check(TileColour(2).getGProperty() == 255 && TileColour(3).getGProperty() == 255,
                  "check D: TextureAddressMode.Clamp repeats the edge texel past U = 1");

            SamplerState wrap = SamplerState::PointWrap;
            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*columns_, doubleWide, &wrap);
            check(TileColour(2).getRProperty() == 255 && TileColour(3).getGProperty() == 255,
                  "check D: TextureAddressMode.Wrap restarts the pattern past U = 1");

            SamplerState mirror;
            mirror.setFilterProperty(TextureFilter::Point);
            mirror.setAddressUProperty(TextureAddressMode::Mirror);
            mirror.setAddressVProperty(TextureAddressMode::Mirror);
            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*columns_, doubleWide, &mirror);
            check(TileColour(2).getGProperty() == 255 && TileColour(3).getRProperty() == 255,
                  "check D: TextureAddressMode.Mirror reflects the pattern past U = 1");
        }

        // Check E -- an unrepresentable sampler is refused, not silently downgraded to Point.
        {
            bool threw = false;
            try
            {
                SamplerState aniso = SamplerState::AnisotropicClamp;
                DrawSprite(*columns_, wholeTexture, &aniso);
            }
            catch (const std::exception&) { threw = true; }
            check(threw,
                  "check E: an anisotropic SamplerState is refused, matching the false capability");

            dev.Clear(Color(0, 0, 255, 255));
            SamplerState point = SamplerState::PointClamp;
            DrawSprite(*columns_, wholeTexture, &point);
            check(TileColour(0).getRProperty() == 255 && TileColour(3).getGProperty() == 255,
                  "check E: a valid batch still draws after the refused one");
        }

        // Check F -- transitions between batches, not just one isolated batch.
        {
            SamplerState pointWrap = SamplerState::PointWrap;
            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*columns_, doubleWide, &pointWrap);
            check(TileColour(2).getRProperty() == 255,
                  "check F: PointWrap applies");

            SamplerState linearClamp = SamplerState::LinearClamp;
            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*columns_, doubleWide, &linearClamp);
            check(TileColour(2).getGProperty() > 200 && TileColour(2).getRProperty() < 60,
                  "check F: switching to LinearClamp replaces both the filter and the wrap mode");

            dev.Clear(Color(0, 0, 255, 255));
            DrawSprite(*columns_, doubleWide, &pointWrap);
            check(TileColour(2).getRProperty() == 255,
                  "check F: switching back to PointWrap restores it");
        }

        Finish();
    }

public:
    PortableGLSamplerTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLSamplerTest game;
    game.Run();
    return game.getResult();
}
