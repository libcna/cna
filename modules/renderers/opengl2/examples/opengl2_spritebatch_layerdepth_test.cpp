// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: adapts examples/easygl_spritebatch_layerdepth_test.cpp's own scene verbatim --
// proves layerDepth-driven draw ORDER (SpriteSortMode::FrontToBack) actually determines which
// sprite is VISIBLE where 2 opaque sprites overlap (painter's algorithm, no depth test, matching
// FNA's own default SpriteBatch behaviour).
//
// 2 overlapping 60x60 opaque sprites, deliberately SUBMITTED in the OPPOSITE order from the
// correct FrontToBack draw order (Draw(B, depth=0.9) first, Draw(A, depth=0.1) second) so this
// test genuinely discriminates depth-based sorting from raw submission order.

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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed (255,   0,   0, 255);
    const Color kBlue(  0,   0, 255, 255);
}

class OpenGL2SpriteBatchLayerDepthTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> redTex_;
    std::unique_ptr<Texture2D> blueTex_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        redTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> redPx(1, kRed);
        redTex_->SetData(redPx.data(), 1);

        blueTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> bluePx(1, kBlue);
        blueTex_->SetData(bluePx.data(), 1);
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb.Begin(SpriteSortMode::FrontToBack,
                 BlendState::Opaque,
                 const_cast<SamplerState*>(&SamplerState::PointClamp),
                 nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb.Draw(*blueTex_, Rectangle(130, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        sb.Draw(*redTex_,  Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);

        sb.End();

        ExpectPixel("A-only-region", Rectangle(115, 130, 1, 1), kRed, /*tolerance=*/60);
        ExpectPixel("overlap-region-B-on-top", Rectangle(145, 130, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("B-only-region", Rectangle(175, 130, 1, 1), kBlue, /*tolerance=*/60);
    }

public:
    OpenGL2SpriteBatchLayerDepthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SpriteBatchLayerDepthTest>();
}
