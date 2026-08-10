// SPDX-License-Identifier: MS-PL
// Task 746: verify SpriteBatch TextureAddressMode::Mirror edge sampling on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_texture_address_mode_mirror_test.cpp (Task 737,
// already reused verbatim on Vulkan). Not a verbatim reuse: that file uses
// GraphicsDevice::SetDepthTestEnabled(false), which throws on Bgfx (a pre-existing, deliberate
// stub), replaced with the equivalent DepthStencilState-based call; and takes all 3 samples
// (Mirror/Wrap/Clamp) within a single Draw() tick with no intervening Present(), incompatible with
// Bgfx's "first GetBackBufferData read per rendered frame" quirk (Task 406) -- restructured into
// one separately-read RunCheck() pass per sample, each its own genuinely fresh rendered frame.
//
// FNA has no "PointMirror" static preset (only *Clamp/*Wrap for Point/Linear/Anisotropic), so this
// builds a custom SamplerState with Filter=Point, AddressU=AddressV=Mirror. Draws a 2x1 (Red|Blue)
// texture via SpriteBatch with a sourceRectangle twice the texture's width (U spans [0,2]), reads
// back at U=1.6 -- deliberately not U=1.25 (Task 750's own Wrap/Clamp test point), since Mirror and
// Clamp coincidentally agree there; at U=1.6 all three modes disagree or Mirror alone stands out:
//   Wrap:   fract(1.6)=0.6           -> right texel -> Blue
//   Clamp:  clamps to the last texel -> right texel -> Blue
//   Mirror: reflects: 2.0-1.6=0.4    -> left texel  -> Red
//
// Exit code 0 = all 3 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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

static constexpr int kSize = 64;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
}

class BgfxTextureAddressModeMirrorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D tex_; // 2x1: [Red, Blue]
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, SamplerState* sampler)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);

            const auto& vp = dev.getViewportProperty();
            const int W = vp.getWidthProperty();
            const int H = vp.getHeightProperty();

            sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, sampler, nullptr, nullptr);
            sb_->Draw(tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 4, 1), Color::White);
            sb_->End();

            const Rectangle region(W * 4 / 5, H / 2, 1, 1); // U = 2*(4/5) = 1.6
            dev.GetBackBufferData(&region, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

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

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        SamplerState pointMirror;
        pointMirror.setFilterProperty(TextureFilter::Point);
        pointMirror.setAddressUProperty(TextureAddressMode::Mirror);
        pointMirror.setAddressVProperty(TextureAddressMode::Mirror);

        const Color mirrorPixel = RunCheck(dev, &pointMirror);
        const Color wrapPixel   = RunCheck(dev, const_cast<SamplerState*>(&SamplerState::PointWrap));
        const Color clampPixel  = RunCheck(dev, const_cast<SamplerState*>(&SamplerState::PointClamp));

        const bool mirrorPass = (mirrorPixel.getRProperty() == 255 && mirrorPixel.getBProperty() == 0);
        const bool wrapIsBlue  = (wrapPixel.getBProperty()  == 255 && wrapPixel.getRProperty()  == 0);
        const bool clampIsBlue = (clampPixel.getBProperty() == 255 && clampPixel.getRProperty() == 0);

        int passCount = 0;
        std::printf("[%s] PointMirror @ U=1.6: got=(%d,%d,%d) want=(255,0,0)\n",
                    mirrorPass ? "PASS" : "FAIL",
                    mirrorPixel.getRProperty(), mirrorPixel.getGProperty(), mirrorPixel.getBProperty());
        if (mirrorPass) ++passCount;
        std::printf("[%s] PointWrap  @ U=1.6 (context: must be Blue, discriminates Mirror from Wrap): got=(%d,%d,%d)\n",
                    wrapIsBlue ? "PASS" : "FAIL",
                    wrapPixel.getRProperty(), wrapPixel.getGProperty(), wrapPixel.getBProperty());
        if (wrapIsBlue) ++passCount;
        std::printf("[%s] PointClamp @ U=1.6 (context: must be Blue, discriminates Mirror from Clamp): got=(%d,%d,%d)\n",
                    clampIsBlue ? "PASS" : "FAIL",
                    clampPixel.getRProperty(), clampPixel.getGProperty(), clampPixel.getBProperty());
        if (clampIsBlue) ++passCount;

        std::printf("=== %d/3 PASS ===\n", passCount);
        result_ = (passCount == 3) ? 0 : 1;
        Exit();
    }

public:
    BgfxTextureAddressModeMirrorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxTextureAddressModeMirrorTest game;
    game.Run();
    return game.getResult();
}
