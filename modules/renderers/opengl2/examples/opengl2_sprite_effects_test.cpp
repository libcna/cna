// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: adapts examples/easygl_sprite_effects_test.cpp's own scene verbatim --
// verifies SpriteEffects::FlipHorizontally and FlipVertically at the UV level.
//
// Viewport 400x100, divided into four 100x100 sections, SamplerState::PointClamp so every pixel
// maps to exactly one texel (no bilinear ambiguity):
//   Section 0: 2x1 texture [Red|Blue], no flip -> left Red, right Blue
//   Section 1: same texture, FlipHorizontally -> left Blue, right Red
//   Section 2: 1x2 texture [Red/Blue], no flip -> top Red, bottom Blue
//   Section 3: same texture, FlipVertically -> top Blue, bottom Red

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed (255,   0,   0, 255);
    const Color kBlue(  0,   0, 255, 255);
}

class OpenGL2SpriteEffectsTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> hTex_;
    std::unique_ptr<Texture2D> vTex_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        hTex_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color hPix[2] = { kRed, kBlue };
        hTex_->SetData(hPix, 2);

        vTex_ = std::make_unique<Texture2D>(dev, 1, 2);
        const Color vPix[2] = { kRed, kBlue };
        vTex_->SetData(vPix, 2);
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb.Begin(SpriteSortMode::Deferred,
                 BlendState::Opaque,
                 const_cast<SamplerState*>(&SamplerState::PointClamp),
                 nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        const Rectangle hFull(0, 0, 2, 1);
        const Rectangle vFull(0, 0, 1, 2);

        sb.Draw(*hTex_, Rectangle(0,   0, 100, 100), hFull,
                Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        sb.Draw(*hTex_, Rectangle(100, 0, 100, 100), hFull,
                Color::White, 0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sb.Draw(*vTex_, Rectangle(200, 0, 100, 100), vFull,
                Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        sb.Draw(*vTex_, Rectangle(300, 0, 100, 100), vFull,
                Color::White, 0.0f, Vector2::Zero, SpriteEffects::FlipVertically, 0.0f);

        sb.End();

        ExpectPixel("S0-left-NoFlip",   Rectangle( 25, 50, 1, 1), kRed,  /*tolerance=*/60);
        ExpectPixel("S0-right-NoFlip",  Rectangle( 75, 50, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("S1-left-FlipH",    Rectangle(125, 50, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("S1-right-FlipH",   Rectangle(175, 50, 1, 1), kRed,  /*tolerance=*/60);
        ExpectPixel("S2-top-NoFlip",    Rectangle(250, 25, 1, 1), kRed,  /*tolerance=*/60);
        ExpectPixel("S2-bottom-NoFlip", Rectangle(250, 75, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("S3-top-FlipV",     Rectangle(350, 25, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("S3-bottom-FlipV",  Rectangle(350, 75, 1, 1), kRed,  /*tolerance=*/60);
    }

public:
    OpenGL2SpriteEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(100);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SpriteEffectsTest>();
}
