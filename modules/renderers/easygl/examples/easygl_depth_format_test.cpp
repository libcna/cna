// SPDX-License-Identifier: MS-PL
// Task 228: Verify DepthStencilFormat changes store correctly and do not crash.
//
// The EasyGL renderer does not recreate the depth buffer at runtime; the window-system
// framebuffer has a fixed depth allocation. The important invariants are:
//   1. The PP field always stores the requested format (field round-trip).
//   2. Neither GDM::ApplyChanges() nor SetPresentationParameters() throws.
//
// Both the GDM path (setPreferredDepthStencilFormat + ApplyChanges) and the
// direct path (GraphicsDevice::SetPresentationParameters) are tested for all
// four DepthFormat values.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DepthFormatTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    void gdmSet(GraphicsDevice& dev, DepthFormat fmt, const char* name)
    {
        gdm_->setPreferredDepthStencilFormatProperty(fmt);
        gdm_->ApplyChanges();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "GDM DepthFormat::%s stored in device PP", name);
        check(dev.getPresentationParametersProperty().getDepthStencilFormatProperty() == fmt, buf);
    }

    void directSet(GraphicsDevice& dev, DepthFormat fmt, const char* name)
    {
        PresentationParameters pp = dev.getPresentationParametersProperty().Clone();
        pp.setDepthStencilFormatProperty(fmt);
        dev.SetPresentationParameters(pp);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Direct SetPP DepthFormat::%s stored", name);
        check(dev.getPresentationParametersProperty().getDepthStencilFormatProperty() == fmt, buf);
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // GDM default is Depth24
        check(dev.getPresentationParametersProperty().getDepthStencilFormatProperty()
              == DepthFormat::Depth24,
              "GDM default DepthFormat::Depth24 stored in device PP");

        // GDM path — all four values
        gdmSet(dev, DepthFormat::Depth16,          "Depth16");
        gdmSet(dev, DepthFormat::Depth24Stencil8,  "Depth24Stencil8");
        gdmSet(dev, DepthFormat::None,             "None");
        gdmSet(dev, DepthFormat::Depth24,          "Depth24");

        // Direct path — all four values
        directSet(dev, DepthFormat::None,            "None");
        directSet(dev, DepthFormat::Depth16,         "Depth16");
        directSet(dev, DepthFormat::Depth24,         "Depth24");
        directSet(dev, DepthFormat::Depth24Stencil8, "Depth24Stencil8");

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DepthFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DepthFormatTest g;
    g.Run();
    return g.getResult();
}
