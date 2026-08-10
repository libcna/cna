// SPDX-License-Identifier: MS-PL
// Task 183: GraphicsDevice device-reset events.
//
// Verifies that GraphicsDeviceManager::DeviceResetting and DeviceReset fire
// when ApplyChanges() is called on an already-initialised device, and that
// DeviceResetting is raised before DeviceReset.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;

class DeviceResetEventsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int resettingSeq_ = 0;
    int resetSeq_     = 0;
    int seqCounter_   = 0;
    int pass_         = 0;
    int fail_         = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();

        // Subscribe to GDM events AFTER construction (so the constructor's
        // implicit ApplyChanges() is not counted).
        gdm_->DeviceResetting += [this](System::Object*, const System::EventArgs&)
        {
            resettingSeq_ = ++seqCounter_;
        };
        gdm_->DeviceReset += [this](System::Object*, const System::EventArgs&)
        {
            resetSeq_ = ++seqCounter_;
        };

        // Trigger a second ApplyChanges() by mutating a preference.
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->ApplyChanges();

        check(resettingSeq_ != 0,              "DeviceResetting fired");
        check(resetSeq_ != 0,                  "DeviceReset fired");
        check(resettingSeq_ < resetSeq_,       "DeviceResetting fires before DeviceReset");
        check(resettingSeq_ == 1,              "DeviceResetting fires first (seq == 1)");
        check(resetSeq_     == 2,              "DeviceReset fires second (seq == 2)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    DeviceResetEventsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DeviceResetEventsTest game;
    game.Run();
    return game.getResult();
}
