// SPDX-License-Identifier: MS-PL
// SpriteBatch rotation-around-origin pixel oracle for the NANOVG renderer.
//
// Same methodology, geometry and expected pixel outcomes as
// openvg_spritebatch_rotation_test.cpp (itself a port of Task 671's SDL_Renderer test) -- the
// decisive test for NanoVgSpriteBatchRenderer's matrix composition (translate, then rotate, then
// scale, then per-sprite origin -- see that class's own header comment): a wrong rotation
// sign/units, or a wrong origin/offset term, produce a wrong pixel here.
//
// Requires PresentationMode::NativeBackBuffer: ReadBackbuffer operates in physical output
// coordinates, and NANOVG's default presentation mode (FixedHeightDynamicWidth) does not map
// logical pixels 1:1 to physical ones.
//
// Design: a 100x100 texture, top-left 20x20 = Red marker, rest = Blue. Drawn at
// destinationRectangle=(200,150,100,100) with origin=(100,100) -- the source's own bottom-right
// corner, diagonally opposite the Red marker -- rotated 90 degrees (PiOver2). The marker's centre
// (source point (10,10)) must land at screen (290,60) after rotation; the origin point itself
// stays fixed at (200,150).
//
// Exit code 0 = all PASS, 1 = at least one FAIL, 77 = SKIPPED (no GPU/display).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kClearColor(0, 255, 0, 255);
}

class NanoVgSpriteBatchRotationTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> tex_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        // 100x100: top-left 20x20 = Red marker, rest = Blue.
        std::vector<uint8_t> pixels(100 * 100 * 4);
        for (int y = 0; y < 100; ++y)
        {
            for (int x = 0; x < 100; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 100 + x) * 4;
                const bool marker = (x < 20 && y < 20);
                const Color& c = marker ? kRed : kBlue;
                pixels[i] = c.getRProperty(); pixels[i + 1] = c.getGProperty();
                pixels[i + 2] = c.getBProperty(); pixels[i + 3] = c.getAProperty();
            }
        }
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 100, 100, pixels));
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kClearColor);
        dev.setBlendStateProperty(BlendState::Opaque);

        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Deferred,
                BlendState::Opaque,
                const_cast<SamplerState*>(&SamplerState::PointClamp),
                nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb.Draw(*tex_,
               Rectangle(200, 150, 100, 100),  // destinationRectangle
               Rectangle(0, 0, 100, 100),      // sourceRectangle (whole texture)
               Color::White,
               MathHelper::PiOver2,            // 90-degree rotation
               Vector2(100.0f, 100.0f),        // origin: source's own bottom-right corner
               SpriteEffects::None,
               0.0f);

        sb.End();

        ExpectPixel("marker-rotated-to-290-60", Rectangle(290, 60, 1, 1), kRed, /*tolerance=*/60);
        ExpectPixel("sprite-interior-away-from-marker", Rectangle(250, 100, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("outside-sprite-entirely", Rectangle(50, 50, 1, 1), kClearColor, /*tolerance=*/60);
    }

public:
    NanoVgSpriteBatchRotationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<NanoVgSpriteBatchRotationTest>();
}
