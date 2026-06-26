// SPDX-License-Identifier: MS-PL
// Task 185: Effect::CurrentTechnique get/set and Techniques[0].Passes[0].Apply().
//
// Verifies that:
//  - getCurrentTechniqueProperty() returns the default "Default" technique after
//    Effect construction.
//  - setCurrentTechniqueProperty() changes what getCurrentTechniqueProperty() returns.
//  - Techniques[0].Passes[0].Apply() completes without crashing (calls OnApply via
//    the effect owner chain).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class EffectCurrentTechniqueTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        GraphicsDevice& device = getGraphicsDeviceProperty();

        AlphaTestEffect fx(device);

        // After construction the default technique must be set.
        EffectTechnique* def = fx.getCurrentTechniqueProperty();
        check(def != nullptr,
              "getCurrentTechniqueProperty() is non-null after construction");
        check(def == &fx.getTechniquesProperty()[0],
              "getCurrentTechniqueProperty() points to Techniques[0]");

        // Techniques[0] is accessible by index and name.
        check(fx.getTechniquesProperty().getCountProperty() >= 1,
              "Techniques has at least one entry");
        check(fx.getTechniquesProperty()["Default"] != nullptr,
              "Techniques[\"Default\"] resolves to non-null");

        // Passes[0] of the default technique exists.
        EffectPassCollection& passes = fx.getTechniquesProperty()[0].getPassesProperty();
        check(passes.getCountProperty() >= 1,
              "Techniques[0].Passes has at least one pass");

        // setCurrentTechniqueProperty round-trip.
        EffectTechnique* t0 = &fx.getTechniquesProperty()[0];
        fx.setCurrentTechniqueProperty(t0);
        check(fx.getCurrentTechniqueProperty() == t0,
              "getCurrentTechniqueProperty() returns the value set via setter");

        // Passes[0].Apply() — calls OnApply via owner, must not crash.
        bool applyOk = false;
        try
        {
            passes[0].Apply();
            applyOk = true;
        }
        catch (...) {}
        check(applyOk, "Techniques[0].Passes[0].Apply() completes without exception");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    EffectCurrentTechniqueTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    EffectCurrentTechniqueTest game;
    game.Run();
    return game.getResult();
}
