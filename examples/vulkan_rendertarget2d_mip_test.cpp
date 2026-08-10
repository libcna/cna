// SPDX-License-Identifier: MS-PL
// Task 878: verify RenderTarget2D's mip chain is genuinely generated with real, correctly
// downsampled content on Vulkan (not just present-but-undefined storage).
//
// Method: render a 7:1 asymmetric split (top 7/8 red, bottom 1/8 blue) into a mipMap=true
// RenderTarget2D via SpriteBatch, unbind (triggers VulkanRenderTargetRenderer::MaybeGenerateMips'
// vkCmdBlitImage cascade -- the Task 878 fix), then sample the unbound RT back two ways:
//   1. At its native 1:1 size (no minification -- only mip level 0 is ever selected): a point well
//      inside the red region and a point well inside the blue region must both read back crisp
//      and unblended.
//   2. Scaled down to a 1x1 on-screen destination (a 64:1 minification ratio, matching kRTSize
//      exactly): a full-quad-to-1-pixel draw always samples the texture's exact centre (u=v=0.5,
//      an inherent property of linear UV interpolation across a quad collapsed to one pixel) --
//      the GPU's automatic derivative-based LOD selection forces that single sample to the
//      coarsest mip level (log2(64)=6 exactly), whose content (per the blit cascade) is the whole
//      image's true weighted average: ~7/8 red + 1/8 blue = (223,0,32).
//
// An EARLIER version of this test used a 50/50 split, which failed to discriminate at all: the
// colour boundary sat exactly at the texture centre, so the forced centre-sample in check 2 hit
// an ordinary bilinear blend across that one boundary row *at level 0 itself*, regardless of
// whether real mips existed -- confirmed empirically (both the fixed and the git-stash-reverted
// build produced an identical (128,0,128) "pass"). The 7:1 split moves the centre sample point
// (row 32 of 64) safely inside the solid red region, so a broken/absent mip chain reads back pure
// red (255,0,0) at check 2 instead of the whole-image average, while check 1's own two sample
// points are unaffected either way (an ordinary 1:1 draw, no forced single-sample derivative
// trick). This differential (crisp red/blue at 1:1, a genuinely blue-tinted average at extreme
// minification) is the discriminating signal.
//
// Also exercises a second, independently-necessary Task 878 fix: every Vulkan VkSampler
// previously had maxLod=0 (VkSamplerCreateInfo{}'s zero-init), clamping every sample to mip level
// 0 regardless of mipmapMode -- harmless for the single-level images every earlier test used, but
// it would have made check 2 read pure red even with a perfectly correct mip-generation cascade.
// Both fixes are required together for this test to pass.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

class RenderTarget2DMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_;
    Texture2D                              patternTex_;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, /*mipMap=*/true,
                                                SurfaceFormat::Color, DepthFormat::None);

        // 1x8: rows 0..6 = red, row 7 = blue -- point-stretched to fill the 64-tall RT below,
        // giving an exact 56-red-rows/8-blue-rows (7:1) split with no seam aliasing (64 is an
        // integer multiple of 8).
        std::vector<uint8_t> pattern;
        pattern.reserve(8 * 4);
        for (int i = 0; i < 7; ++i) { pattern.insert(pattern.end(), {255, 0, 0, 255}); }
        pattern.insert(pattern.end(), {0, 0, 255, 255});
        patternTex_ = Texture2D::CreateFromPixels(device, 1, 8, pattern);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);

        // --- Phase 1: fill level 0 with the 7:1 red/blue split, then unbind (triggers mip
        // regeneration). Point filter: patternTex_ only has 1 level, so this draw is not itself
        // subject to the thing under test. ---
        device.SetRenderTarget(rt_.get());
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(patternTex_, Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Check 1: native 1:1 size -- no minification, level 0 only, must stay crisp. Sample
        // points are well inside each region (row 16 of 0..55 red, row 60 of 56..63 blue). ---
        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(*rt_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        Color topPx(0, 0, 0, 0), botPx(0, 0, 0, 0);
        const Rectangle topReg(kRTSize / 2, 16, 1, 1);
        const Rectangle botReg(kRTSize / 2, 60, 1, 1);
        device.GetBackBufferData(&topReg, &topPx, 0, 1);
        device.GetBackBufferData(&botReg, &botPx, 0, 1);

        const bool topCrisp = topPx.getRProperty() >= 200 && topPx.getBProperty() <= 30;
        const bool botCrisp = botPx.getBProperty() >= 200 && botPx.getRProperty() <= 30;
        const bool check1 = topCrisp && botCrisp;

        // --- Check 2: scaled down to a 1x1 destination (64:1 minification, log2(64)=6 exactly)
        // -- always samples the texture's exact centre (row 32, deep inside the red region),
        // forced to the single coarsest mip level, whose content (per the blit cascade) is the
        // whole image's true 7:1 weighted average, ~(223,0,32) -- NOT pure red. ---
        device.Clear(Color(0, 0, 0, 255));
        SamplerState linear = SamplerState::LinearClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linear, nullptr, nullptr);
        sb_->Draw(*rt_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();

        Color avgPx(0, 0, 0, 0);
        const Rectangle avgReg(0, 0, 1, 1);
        device.GetBackBufferData(&avgReg, &avgPx, 0, 1);

        const bool check2 = avgPx.getRProperty() >= 190 && avgPx.getRProperty() <= 240 &&
                            avgPx.getBProperty() >= 15  && avgPx.getBProperty() <= 55  &&
                            avgPx.getGProperty() <= 20;

        std::printf("[%s] Check 1 (1:1, level 0 only): top=(%d,%d,%d) bottom=(%d,%d,%d), "
                    "expected crisp red/blue\n",
                    check1 ? "PASS" : "FAIL",
                    topPx.getRProperty(), topPx.getGProperty(), topPx.getBProperty(),
                    botPx.getRProperty(), botPx.getGProperty(), botPx.getBProperty());
        std::printf("[%s] Check 2 (64:1 minification, coarsest mip, forced centre sample): "
                    "sample=(%d,%d,%d), expected ~7:1 weighted average (223,0,32), not pure red\n",
                    check2 ? "PASS" : "FAIL",
                    avgPx.getRProperty(), avgPx.getGProperty(), avgPx.getBProperty());

        result_ = (check1 && check2) ? 0 : 1;
        Exit();
    }

public:
    RenderTarget2DMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    RenderTarget2DMipTest game;
    game.Run();
    return game.getResult();
}
