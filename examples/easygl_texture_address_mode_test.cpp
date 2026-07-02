// SPDX-License-Identifier: MS-PL
// Task 269: EasyGL integration test — SpriteBatch TextureAddressMode edge sampling.
//
// Draws a 2x1 (Red|Blue) texture via SpriteBatch with a sourceRectangle twice the
// texture's width, so the sampled U coordinate spans [0,2] — exercising sampling
// past the texture edge, the classic XNA scrolling/tiling-background technique
// (FNA's real SpriteBatch never clamps sourceRectangle-derived UVs to [0,1]).
//
// Reads back the pixel at U≈1.25 (75% into the second [1,2) repeat's first half):
//   - PointWrap:  wraps (fract(1.25)=0.25) → left texel → Red.
//   - PointClamp: clamps to the texture's last texel → Blue.
// Point filtering keeps samples crisp (no bilinear blending) for exact comparison.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
}

class TextureAddressModeTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    tex_; // 2x1: [Red, Blue]
    bool                         done_   = false;
    int                          result_ = 1; // 1 = fail until proven otherwise

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        const std::vector<std::uint8_t> px = {
            kRed.getRProperty(),  kRed.getGProperty(),  kRed.getBProperty(),  kRed.getAProperty(),
            kBlue.getRProperty(), kBlue.getGProperty(), kBlue.getBProperty(), kBlue.getAProperty(),
        };
        tex_ = Texture2D::CreateFromPixels(device, 2, 1, px);
    }

    // Draws the 2x1 texture across the full viewport with a sourceRectangle 2x the
    // texture width (U spans [0,2]) using the given sampler preset, then reads back
    // the pixel at 5/8 of the viewport width (U = 2*(5/8) = 1.25).
    Color SampleAtUOnePointTwoFive(SamplerState* sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, sampler, nullptr, nullptr);
        sb_->Draw(tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 4, 1), Color::White);
        sb_->End();

        const Rectangle region(W * 5 / 8, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        getGraphicsDeviceProperty().SetDepthTestEnabled(false);

        const Color wrapPixel  = SampleAtUOnePointTwoFive(const_cast<SamplerState*>(&SamplerState::PointWrap));
        const Color clampPixel = SampleAtUOnePointTwoFive(const_cast<SamplerState*>(&SamplerState::PointClamp));

        const bool wrapPass  = (wrapPixel.getRProperty()  == 255 && wrapPixel.getBProperty()  == 0);
        const bool clampPass = (clampPixel.getRProperty() == 0   && clampPixel.getBProperty() == 255);

        std::printf("[%s] PointWrap  @ U=1.25: got=(%d,%d,%d) want=(255,0,0)\n",
                    wrapPass ? "PASS" : "FAIL",
                    wrapPixel.getRProperty(), wrapPixel.getGProperty(), wrapPixel.getBProperty());
        std::printf("[%s] PointClamp @ U=1.25: got=(%d,%d,%d) want=(0,0,255)\n",
                    clampPass ? "PASS" : "FAIL",
                    clampPixel.getRProperty(), clampPixel.getGProperty(), clampPixel.getBProperty());

        result_ = (wrapPass && clampPass) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    TextureAddressModeTest game;
    game.Run();
    return game.getResult();
}
