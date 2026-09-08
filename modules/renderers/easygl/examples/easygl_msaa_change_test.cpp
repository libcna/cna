// SPDX-License-Identifier: MS-PL
// Task 229/SOFTWARE-182: Verify MSAA MultiSampleCount changes after device creation.
//
// EasyGL owns a faux multisample backbuffer FBO, so Reset can replace those attachments without
// recreating the GL context. GraphicsDevice writes the real driver-clamped value returned by
// ApplyMultiSampleCount() back into PresentationParameters, matching FNA's reset contract.
//
// The invariants this test verifies:
//   1. GDM with preferMultiSampling=false → device PP stores MultiSampleCount=0.
//   2. GDM with preferMultiSampling=true on the already-created device allocates a real supported
//      count and reports the same count from both GraphicsDevice and the renderer.
//   3. Disabling releases that storage.
//   4. CNAEXT SetPresentationParameters remains store-only but reports the currently applied count
//      instead of echoing an unapplied request.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
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

    void directSet(GraphicsDevice& dev, int count, int expected, const char* label)
    {
        PresentationParameters pp = dev.getPresentationParametersProperty().Clone();
        pp.setMultiSampleCountProperty(count);
        dev.SetPresentationParameters(pp);
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   expected, label);
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // GDM default: preferMultiSampling=false → MultiSampleCount=0
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   0, "GDM default MultiSampleCount=0 (preferMultiSampling=false)");

        // Enable MSAA via GDM on an already-constructed EasyGL device.
        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->ApplyChanges();
        const int applied = dev.getPresentationParametersProperty().getMultiSampleCountProperty();
        check(applied > 1 && applied <= 8,
              "GDM preferMultiSampling=true allocates a supported multisample count");
        checkCount(dev.GetRenderer().GetMultiSampleCount(), applied,
                   "renderer and PresentationParameters report the same applied count");
        check(gdm_->getPreferMultiSamplingProperty(),
              "GDM getter returns true after setPreferMultiSampling(true)");

        // Disable again — PP stores 0
        gdm_->setPreferMultiSamplingProperty(false);
        gdm_->ApplyChanges();
        checkCount(dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                   0, "GDM preferMultiSampling=false → MultiSampleCount=0 in PP");

        // Direct store-only path — the renderer stays single-sample and the stored state reports
        // that applied result rather than an arbitrary request.
        directSet(dev, 0, 0, "Direct SetPP MultiSampleCount=0 reports applied zero");
        directSet(dev, 1, 0, "Direct SetPP MultiSampleCount=1 reports applied zero");
        directSet(dev, 2, 0, "Direct SetPP MultiSampleCount=2 reports applied zero");
        directSet(dev, 4, 0, "Direct SetPP MultiSampleCount=4 reports applied zero");
        directSet(dev, 8, 0, "Direct SetPP MultiSampleCount=8 reports applied zero");

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
