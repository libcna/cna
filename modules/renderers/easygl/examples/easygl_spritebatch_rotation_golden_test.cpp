// SPDX-License-Identifier: MS-PL
// Task 465: golden-image consumer reusing Phase 47's already-verified SpriteBatch
// rotation-around-origin scene (Task 417, examples/easygl_spritebatch_rotation_test.cpp) via
// the PixelTestGame::CompareGoldenImage() helper (Task 463) instead of Task 417's own 3
// hand-picked sample pixels.
//
// Same scene as Task 417 exactly: a 100x100 texture (top-left 20x20 = Red marker, rest = Blue)
// drawn at destinationRectangle=(200,150,100,100) with origin=(100,100) (the source's own
// bottom-right corner) rotated 90 degrees (PiOver2). Per Task 417's own derivation, the rotated
// marker's screen footprint is the axis-aligned square x:(280,300], y:[50,70), centred on
// (290,60) -- this test crops an 8x8 region centred there (margin of 6px from the nearest edge
// of that footprint, comfortably clear of any rasterization edge effects) instead of a single
// point sample.
//
// Reuses Task 417's own tolerance=60 (its `colourMatch` default).

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

class SpriteBatchRotationGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        // 100x100: top-left 20x20 = Red marker, rest = Blue -- identical to Task 417.
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
                Rectangle(200, 150, 100, 100),   // destinationRectangle
                Rectangle(0, 0, 100, 100),        // sourceRectangle (whole texture)
                Color::White,
                MathHelper::PiOver2,               // 90-degree rotation
                Vector2(100.0f, 100.0f),           // origin: source's own bottom-right corner
                SpriteEffects::None,
                0.0f);

        sb.End();

        // Cross-check against Task 417's own hardcoded expected marker value, independent of
        // whatever is actually stored in the golden PNG below (same rationale as Task 464).
        ExpectPixel("marker-vs-task417-expected", Rectangle(290, 60, 1, 1), kRed, /*tolerance=*/60);
        CompareGoldenImage("spritebatch-rotated-marker",
                            Rectangle(290 - 4, 60 - 4, 8, 8),
                            "examples/golden/easygl_spritebatch_rotation_golden_test.png",
                            /*tolerance=*/60);
    }

public:
    SpriteBatchRotationGoldenTest()
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
    return CNA::Examples::RunPixelTest<SpriteBatchRotationGoldenTest>();
}
