// SPDX-License-Identifier: MS-PL
// Task 182: PresentationParameters round-trip via GraphicsDeviceManager.
//
// GraphicsDeviceManager::ApplyChanges() (called implicitly at Game startup)
// propagates the "preferred" settings into GraphicsDevice::PresentationParameters.
// This test verifies that the five fields named in the task reflect the requested
// values after the device is created:
//   BackBufferWidth, BackBufferHeight, DepthStencilFormat,
//   PresentInterval, MultiSampleCount.
//
// Note: PresentationInterval maps as follows —
//   synchronizeWithVerticalRetrace = true  → PresentInterval::One
//   synchronizeWithVerticalRetrace = false → PresentInterval::Immediate

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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
        check(pp.getDepthStencilFormatProperty() == DepthFormat::Depth24,
              "DepthStencilFormat matches requested value");
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
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
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
