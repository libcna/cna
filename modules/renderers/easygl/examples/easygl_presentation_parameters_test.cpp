// SPDX-License-Identifier: MS-PL
// Task 182: PresentationParameters round-trip via GraphicsDeviceManager.
//
// GraphicsDeviceManager::ApplyChanges() (called implicitly at Game startup)
// propagates the "preferred" settings into GraphicsDevice::PresentationParameters.
// This test verifies that the five fields named in the task reflect values the
// renderer actually applied after the device is created:
//   BackBufferWidth, BackBufferHeight, DepthStencilFormat,
//   PresentInterval, MultiSampleCount.
//
// Note: PresentationInterval maps as follows —
//   synchronizeWithVerticalRetrace = true  → PresentInterval::One
//   synchronizeWithVerticalRetrace = false → PresentInterval::Immediate

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kW = 320;
static constexpr int kH = 240;

class PresentationParametersTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_   = 0;
    int fail_   = 0;
    int result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else { ++fail_; result_ = 1; }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        const PresentationParameters& pp =
            getGraphicsDeviceProperty().getPresentationParametersProperty();

        check(pp.getBackBufferWidthProperty()  == kW,
              "BackBufferWidth matches requested value");
        check(pp.getBackBufferHeightProperty() == kH,
              "BackBufferHeight matches requested value");
        // Both branches asserted this differently and BOTH were green on their own tree, which is
        // the whole reason it needed care. plans/plan_vulkan.md VULKAN-335 asks the renderer what it
        // applied -- the more honest question in general, and right for Vulkan, which really does
        // substitute (FindDepthFormat prefers D24_UNORM_S8_UINT so StencilEnable can work).
        //
        // It is NOT adopted here, because on EasyGL it contradicts EasyGL_DepthFormat, which
        // requires a requested Depth24 to be STORED as Depth24 and was green before this merge.
        // Satisfying the renderer-query form by teaching EasyGL to answer Depth24Stencil8 was tried
        // during the merge (2026-09-11) and turned EasyGL_DepthFormat red: one test's premise
        // cannot be bought with another's. Whether EasyGL's back buffer really is packed
        // depth+stencil -- and so whether its query or this expectation is the one that should
        // change -- was not established, and is left open rather than guessed at.
        check(pp.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              "DepthStencilFormat matches the applied D24S8 value");
        check(pp.getPresentationIntervalProperty() == PresentInterval::Immediate,
              "PresentInterval is Immediate (synchronizeWithVerticalRetrace=false)");
        check(pp.getMultiSampleCountProperty() == 0,
              "MultiSampleCount is 0 (preferMultiSampling=false)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    PresentationParametersTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kW);
        gdm_->setPreferredBackBufferHeightProperty(kH);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        gdm_->setPreferMultiSamplingProperty(false);
        gdm_->ApplyChanges();
    }

    int getResult() const { return result_; }
};

int main()
{
    PresentationParametersTest game;
    game.Run();
    return game.getResult();
}
