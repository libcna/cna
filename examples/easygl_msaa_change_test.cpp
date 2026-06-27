// SPDX-License-Identifier: MS-PL
// Task 229: Verify MSAA MultiSampleCount changes after device creation.
//
// MultiSampleCount is applied to the EasyGL backend only at construction time
// (via GraphicsBackendCreateArgs::multiSampleCount). There is no
// IGraphicsBackend::SetMultiSampleCount() — changing the sample count at
// runtime would require recreating the backend, which is not yet implemented.
//
// The invariants this test verifies:
//   1. GDM with preferMultiSampling=false → device PP stores MultiSampleCount=0.
//   2. GDM with preferMultiSampling=true  → device PP stores MultiSampleCount=8
//      (CNA caps MSAA count at 8 when preferMultiSampling is true, matching the
//       FNA behavior of capping at the max supported count; actual GL cap may
//       reduce this value further but is transparent to PP storage).
//   3. Toggling preferMultiSampling after device creation via ApplyChanges()
//      updates the PP field — does not throw.
//   4. Direct GraphicsDevice::SetPresentationParameters() with arbitrary
//      sample counts stores the value — does not throw.
//
// What is NOT tested: actual MSAA rendering quality (requires pixel readback
// and a rendered scene with geometry edges — out of scope for this task).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MsaaChangeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    void checkCount(int actual, int expected, const char* label)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: expected %d, got %d", label, expected, actual);
        check(actual == expected, buf);
    }

    void directSet(GraphicsDevice& dev, int count, const char* label)
    {
        PresentationParameters pp = dev.getPresentationParametersProperty().Clone();
        pp.setMultiSampleCountProperty(count);
        dev.SetPresentationParameters(pp);
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   count, label);
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // GDM default: preferMultiSampling=false → MultiSampleCount=0
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   0, "GDM default MultiSampleCount=0 (preferMultiSampling=false)");

        // Enable MSAA via GDM — PP stores 8 (CNA cap when preferMultiSampling=true)
        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->ApplyChanges();
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   8, "GDM preferMultiSampling=true → MultiSampleCount=8 in PP");
        check(gdm_->getPreferMultiSamplingProperty(),
              "GDM getter returns true after setPreferMultiSampling(true)");

        // Disable again — PP stores 0
        gdm_->setPreferMultiSamplingProperty(false);
        gdm_->ApplyChanges();
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   0, "GDM preferMultiSampling=false → MultiSampleCount=0 in PP");

        // Direct path — arbitrary values round-trip in PP; backend is unchanged
        directSet(dev, 0, "Direct SetPP MultiSampleCount=0 stored");
        directSet(dev, 1, "Direct SetPP MultiSampleCount=1 stored");
        directSet(dev, 2, "Direct SetPP MultiSampleCount=2 stored");
        directSet(dev, 4, "Direct SetPP MultiSampleCount=4 stored");
        directSet(dev, 8, "Direct SetPP MultiSampleCount=8 stored");

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    MsaaChangeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    MsaaChangeTest g;
    g.Run();
    return g.getResult();
}
