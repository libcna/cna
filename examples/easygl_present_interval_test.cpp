// SPDX-License-Identifier: MS-PL
// Task 223: Verify PresentInterval mapping is accepted without crash.
//
// EasyGL: SDL_GL_SetSwapInterval is called.
// Actual VSync timing is a display effect — cannot be verified headlessly.
// This test verifies:
//   - Default GDM (SynchronizeWithVerticalRetrace=true) → PresentInterval::One stored.
//   - Disabling VSync via GDM → PresentInterval::Immediate stored.
//   - Runtime SetPresentationParameters with Two / Default / Immediate does not crash.
//   - All stored values round-trip correctly.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class PresentIntervalTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool syncVRetrace_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    void setAndCheck(GraphicsDevice& dev, PresentInterval pi, const char* name)
    {
        PresentationParameters pp = dev.getPresentationParametersProperty().Clone();
        pp.setPresentationIntervalProperty(pi);
        dev.SetPresentationParameters(pp);
        bool ok = dev.getPresentationParametersProperty().getPresentationIntervalProperty() == pi;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "SetPresentationParameters(PresentInterval::%s) round-trip", name);
        check(ok, buf);
    }

protected:
    void Initialize() override
    {
        // GDM::ApplyChanges() was already called in GDM ctor (with default sync=true).
        // setSynchronizeWithVerticalRetrace() marks prefs dirty but does not re-apply.
        // Explicitly call ApplyChanges() so the new interval takes effect before Draw().
        gdm_->ApplyChanges();
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // GDM sets interval based on SynchronizeWithVerticalRetrace.
        PresentInterval expected = syncVRetrace_
            ? PresentInterval::One
            : PresentInterval::Immediate;
        check(dev.getPresentationParametersProperty().getPresentationIntervalProperty() == expected,
              "Initial PresentInterval matches SynchronizeWithVerticalRetrace setting");

        // Runtime changes via SetPresentationParameters must not crash.
        setAndCheck(dev, PresentInterval::Immediate, "Immediate");
        setAndCheck(dev, PresentInterval::One,       "One");
        setAndCheck(dev, PresentInterval::Two,       "Two");
        setAndCheck(dev, PresentInterval::Default,   "Default");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    explicit PresentIntervalTest(bool syncVRetrace)
        : syncVRetrace_(syncVRetrace)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setSynchronizeWithVerticalRetraceProperty(syncVRetrace);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    int grandFail = 0;

    std::printf("--- SynchronizeWithVerticalRetrace = true (VSync on) ---\n");
    { PresentIntervalTest g(true);  g.Run(); grandFail += g.getResult(); }

    std::printf("--- SynchronizeWithVerticalRetrace = false (VSync off) ---\n");
    { PresentIntervalTest g(false); g.Run(); grandFail += g.getResult(); }

    return grandFail > 0 ? 1 : 0;
}
