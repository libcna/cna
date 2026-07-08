// SPDX-License-Identifier: MS-PL
// Task 737: EasyGL integration test — SpriteBatch TextureAddressMode::Mirror edge sampling.
//
// Task 269 fixed SpriteBatch::Begin()'s SamplerState (Filter/AddressU/AddressV) having no effect
// on EasyGL, and its own test (easygl_texture_address_mode_test.cpp) independently pixel-verified
// Wrap and Clamp — but never Mirror, even though Mirror is fixed by the exact same
// SetSamplerFilter/SetSamplerAddressMode code path. This test closes that gap.
//
// FNA has no "PointMirror" static preset (only *Clamp/*Wrap for Point/Linear/Anisotropic), so this
// test builds a custom SamplerState with Filter=Point, AddressU=AddressV=Mirror, exactly like
// Task 296's DualTextureEffect-based Mirror test (easygl_texture_address_mode_mirror_effect_test.cpp)
// — but exercised through SpriteBatch instead of DrawUserPrimitives+Effect, the code path Task 269
// actually fixed.
//
// Draws a 2x1 (Red|Blue) texture via SpriteBatch with a sourceRectangle twice the texture's width,
// so the sampled U spans [0,2] (the classic XNA scrolling/tiling technique; FNA's real SpriteBatch
// never clamps sourceRectangle-derived UVs to [0,1]).
//
// Reads back the pixel at destination x-fraction 0.8, i.e. raw U=1.6 — deliberately NOT the U=1.25
// point Task 269's own test uses, since Mirror and Clamp coincidentally agree at U=1.25 (both land
// on the last texel). At U=1.6, all three modes disagree or Mirror alone stands out:
//   - Wrap:   fract(1.6)=0.6            -> right texel -> Blue
//   - Clamp:  clamps to the last texel  -> right texel -> Blue
//   - Mirror: reflects: 2.0-1.6=0.4     -> left texel  -> Red
// Point filtering keeps samples crisp (no bilinear blending) for exact comparison.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

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

class TextureAddressModeMirrorTest : public Game
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

    // Draws the 2x1 texture across the full viewport with a sourceRectangle 2x the texture width
    // (U spans [0,2]) using the given sampler, then reads back the pixel at 4/5 of the viewport
    // width (U = 2*(4/5) = 1.6).
    Color SampleAtUOnePointSix(SamplerState* sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, sampler, nullptr, nullptr);
        sb_->Draw(tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 4, 1), Color::White);
        sb_->End();

        const Rectangle region(W * 4 / 5, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        getGraphicsDeviceProperty().SetDepthTestEnabled(false);

        SamplerState pointMirror;
        pointMirror.setFilterProperty(TextureFilter::Point);
        pointMirror.setAddressUProperty(TextureAddressMode::Mirror);
        pointMirror.setAddressVProperty(TextureAddressMode::Mirror);

        const Color mirrorPixel = SampleAtUOnePointSix(&pointMirror);
        const Color wrapPixel   = SampleAtUOnePointSix(const_cast<SamplerState*>(&SamplerState::PointWrap));
        const Color clampPixel  = SampleAtUOnePointSix(const_cast<SamplerState*>(&SamplerState::PointClamp));

        const bool mirrorPass = (mirrorPixel.getRProperty() == 255 && mirrorPixel.getBProperty() == 0);
        const bool wrapIsBlue  = (wrapPixel.getBProperty()  == 255 && wrapPixel.getRProperty()  == 0);
        const bool clampIsBlue = (clampPixel.getBProperty() == 255 && clampPixel.getRProperty() == 0);

        std::printf("[%s] PointMirror @ U=1.6: got=(%d,%d,%d) want=(255,0,0)\n",
                    mirrorPass ? "PASS" : "FAIL",
                    mirrorPixel.getRProperty(), mirrorPixel.getGProperty(), mirrorPixel.getBProperty());
        std::printf("[context] PointWrap  @ U=1.6: got=(%d,%d,%d) (Blue expected, not asserted here)\n",
                    wrapPixel.getRProperty(), wrapPixel.getGProperty(), wrapPixel.getBProperty());
        std::printf("[context] PointClamp @ U=1.6: got=(%d,%d,%d) (Blue expected, not asserted here)\n",
                    clampPixel.getRProperty(), clampPixel.getGProperty(), clampPixel.getBProperty());

        // Mirror's own PASS is the actual subject of this test; Wrap/Clamp are printed only to
        // confirm this sample point genuinely discriminates Mirror from both (both must read
        // Blue here, unlike U=1.25 where Clamp and Mirror coincidentally agree).
        result_ = (mirrorPass && wrapIsBlue && clampIsBlue) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    TextureAddressModeMirrorTest game;
    game.Run();
    return game.getResult();
}
