// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: adapts examples/easygl_spritebatch_scale_test.cpp's own scene verbatim --
// verifies SpriteBatch::Draw(texture, position, sourceRectangle, color, rotation, origin, scale,
// effects, layerDepth) -- both the scalar float and Vector2 scale overloads -- actually resize
// the drawn sprite's on-screen destination rectangle by the given factor(s) relative to the
// source rectangle.
//
// A single 20x20 solid-Red texture, drawn twice against a Green background:
//   Draw 1 (scalar overload): position=(50,50), scale=3.0f -> expected 60x60 destination
//     rectangle spanning screen x:[50,110), y:[50,110).
//   Draw 2 (Vector2 overload): position=(200,50), scale=(2.0f,4.0f) (non-uniform) -> expected
//     40x80 destination rectangle spanning screen x:[200,240), y:[50,130).
//
// Check points for Draw 2 are chosen to discriminate a correct non-uniform implementation from
// 2 plausible bugs: using only scale.X for both axes, or only scale.Y for both axes.
//
//   Draw 3 (scalar overload, non-zero origin): position=(300,150), origin=(10,10) (the source
//     rectangle's own centre), scale=2.0f -> expected 40x40 destination rectangle CENTRED on
//     position, spanning screen x:[280,320), y:[130,170). Regression check for a real bug (see
//     ComputeSpriteScreenCorners()'s own fix comment, OpenGL2Renderer.cpp): origin is
//     defined in SOURCE-rectangle pixel units and must be scaled by (destination/source) before
//     being applied against the destination rectangle -- using it unscaled shifts the sprite by
//     `origin - origin*(dest/source)` = a 10px down-right offset here (destination would instead
//     span x:[290,330), y:[140,180), asymmetric around position instead of centred on it).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed  (255,   0,   0, 255);
    const Color kGreen(  0, 255,   0, 255);
}

class OpenGL2SpriteBatchScaleTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> tex_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        tex_ = std::make_unique<Texture2D>(dev, 20, 20);
        std::vector<Color> pixels(20 * 20, kRed);
        tex_->SetData(pixels.data(), static_cast<int>(pixels.size()));
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        dev.Clear(kGreen);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb.Begin(SpriteSortMode::Deferred,
                 BlendState::Opaque,
                 const_cast<SamplerState*>(&SamplerState::PointClamp),
                 nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb.Draw(*tex_, Vector2(50.0f, 50.0f), std::optional<Rectangle>{}, Color::White,
                0.0f, Vector2::Zero, 3.0f, SpriteEffects::None, 0.0f);

        sb.Draw(*tex_, Vector2(200.0f, 50.0f), std::optional<Rectangle>{}, Color::White,
                0.0f, Vector2::Zero, Vector2(2.0f, 4.0f), SpriteEffects::None, 0.0f);

        sb.Draw(*tex_, Vector2(300.0f, 150.0f), std::optional<Rectangle>{}, Color::White,
                0.0f, Vector2(10.0f, 10.0f), 2.0f, SpriteEffects::None, 0.0f);

        sb.End();

        ExpectPixel("scalar-scale-3-interior", Rectangle(100, 100, 1, 1), kRed, /*tolerance=*/60);
        ExpectPixel("scalar-scale-3-past-edge", Rectangle(115, 80, 1, 1), kGreen, /*tolerance=*/60);
        ExpectPixel("vector2-scale-2-4-interior", Rectangle(220, 110, 1, 1), kRed, /*tolerance=*/60);
        ExpectPixel("vector2-scale-2-4-past-edge", Rectangle(245, 70, 1, 1), kGreen, /*tolerance=*/60);
        ExpectPixel("far-from-both-sprites", Rectangle(50, 250, 1, 1), kGreen, /*tolerance=*/60);
        // Draw 3: origin scaled correctly -> centred 40x40 square, x:[280,320) y:[130,170).
        ExpectPixel("origin-scale-2-centre", Rectangle(300, 150, 1, 1), kRed, /*tolerance=*/60);
        ExpectPixel("origin-scale-2-left-of-unscaled-origin-shift", Rectangle(285, 150, 1, 1), kRed, /*tolerance=*/60);
        ExpectPixel("origin-scale-2-right-past-correct-edge", Rectangle(325, 150, 1, 1), kGreen, /*tolerance=*/60);
    }

public:
    OpenGL2SpriteBatchScaleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SpriteBatchScaleTest>();
}
