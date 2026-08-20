// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_spritebatch_rotation_golden_test.cpp's own SpriteBatch rotation-around-origin
// scene verbatim: a 100x100 texture (top-left 20x20 = Red marker, rest = Blue) drawn at
// destinationRectangle=(200,150,100,100) with origin=(100,100) (the source's own bottom-right
// corner) rotated 90 degrees (PiOver2). The rotated marker's screen footprint is the
// axis-aligned square x:(280,300], y:[50,70), centred on (290,60).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed  (255,   0,   0, 255);
    const Color kBlue (  0,   0, 255, 255);
    const Color kClear(  0, 255,   0, 255);
}

class OpenGL2SpriteBatchRotationGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        Texture2D tex(dev, 100, 100);
        std::vector<Color> pixels(100 * 100, kBlue);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                pixels[y * 100 + x] = kRed;
        tex.SetData(pixels.data(), static_cast<int>(pixels.size()));

        dev.Clear(kClear);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb.Begin(SpriteSortMode::Deferred,
                 BlendState::Opaque,
                 const_cast<SamplerState*>(&SamplerState::PointClamp),
                 nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb.Draw(tex,
                Rectangle(200, 150, 100, 100),
                Rectangle(0, 0, 100, 100),
                Color::White,
                MathHelper::PiOver2,
                Vector2(100.0f, 100.0f),
                SpriteEffects::None,
                0.0f);

        sb.End();

        ExpectPixel("marker-vs-expected", Rectangle(290, 60, 1, 1), kRed, /*tolerance=*/60);
        CompareGoldenImage("spritebatch-rotated-marker-vs-easygl",
                            Rectangle(290 - 4, 60 - 4, 8, 8),
                            "examples/golden/easygl_spritebatch_rotation_golden_test.png",
                            /*tolerance=*/60);
    }

public:
    OpenGL2SpriteBatchRotationGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> gdm_;
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SpriteBatchRotationGoldenTest>();
}
