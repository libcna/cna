// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-14: the 2D pipeline end to end -- SpriteBatch through the IGL renderer's own
// dynamic vertex/index pool, uber-effect shader, sampler state and blend state, verified by reading
// the rendered pixels back.
//
// Scene: a red-cleared 64x64 back buffer, one opaque green 16x16 sprite at (8,8), and one
// half-alpha blue 16x16 sprite at (40,40) drawn with NonPremultiplied so the destination shows
// through. AlphaBlend would be wrong here: it is XNA's PREmultiplied preset (One /
// InverseSourceAlpha), under which this non-premultiplied half-alpha tint would legitimately come
// out as fully opaque blue instead of mixing with the background (see llgl_2d_test.cpp for the
// same distinction).
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
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
    constexpr int kSize = 64;
    constexpr int kSpriteSize = 16;
}

class Igl2DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        auto white = std::make_unique<Texture2D>(device, kSpriteSize, kSpriteSize);
        std::vector<Color> pixels(static_cast<std::size_t>(kSpriteSize * kSpriteSize),
                                  Color(static_cast<bytecs>(255), static_cast<bytecs>(255),
                                        static_cast<bytecs>(255), static_cast<bytecs>(255)));
        white->SetData(pixels.data(), static_cast<int>(pixels.size()));

        if (!ExpectTrue("the sprite texture was created", white != nullptr))
            return;

        device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));

        SpriteBatch batch(device);

        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        batch.Draw(*white, Rectangle(8, 8, kSpriteSize, kSpriteSize),
                   Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                         static_cast<bytecs>(0), static_cast<bytecs>(255)));
        batch.End();

        batch.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        batch.Draw(*white, Rectangle(40, 40, kSpriteSize, kSpriteSize),
                   Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                         static_cast<bytecs>(255), static_cast<bytecs>(128)));
        batch.End();

        // The cleared background outside every sprite.
        ExpectPixel("background stays the clear colour", Rectangle(2, 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));

        // The opaque sprite replaces the background exactly, which also proves the sprite's
        // destination rectangle lands where XNA's top-left origin says it should.
        ExpectPixel("opaque sprite centre is its tint", Rectangle(8 + kSpriteSize / 2,
                                                                 8 + kSpriteSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));

        // Just outside the opaque sprite: proves the quad is not oversized and that Y is not
        // flipped -- a vertically mirrored sprite would land at (8, 64-8-16) instead.
        ExpectPixel("just above the opaque sprite is still background", Rectangle(16, 6, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));

        // Half-alpha blue over red: 128/255 blue mixed with the remaining red.
        ExpectPixel("alpha-blended sprite mixes with the background",
                    Rectangle(40 + kSpriteSize / 2, 40 + kSpriteSize / 2, 1, 1),
                    Color(static_cast<bytecs>(127), static_cast<bytecs>(0),
                          static_cast<bytecs>(128), static_cast<bytecs>(255)),
                    /*tolerance=*/6);
    }

public:
    Igl2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Igl2DTest>();
}
