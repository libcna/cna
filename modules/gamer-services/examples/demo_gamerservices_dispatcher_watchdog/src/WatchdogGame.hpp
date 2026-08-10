#pragma once

#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// Task 15.12: cna_demo_gamerservices_dispatcher_watchdog. A visual/interactive version of
// tools/net/gamerservices_dispatcher_harness.cpp (which already proves, via exit code, that Task
// 12.1's/Task 7.1's historical hangs are fixed). This demo makes the same proof *visible*: each
// step renders "waiting..." for a short warm-up window (long enough for a human watching the
// window to actually perceive it) before making the real, once-hanging-forever blocking call, then
// flips to "SUCCESS (Xms)" showing the real measured wall-clock duration - if any of these three
// regressed back to their historical hang, the "waiting..." text for that step would simply never
// change, visible directly in the window rather than requiring a human to trust a silent exit code
// or a killed/timed-out process.
class WatchdogGame : public Microsoft::Xna::Framework::Game
{
public:
    WatchdogGame();

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

private:
    enum class Step
    {
        WarmupInitialize,
        RunInitialize,
        WarmupCreate,
        RunCreate,
        WarmupAchievements,
        RunAchievements,
        AllDone
    };

    struct StepResult
    {
        bool done = false;
        double elapsedMs = 0.0;
    };

    void AdvanceStateMachine();

    Step step_ = Step::WarmupInitialize;
    int framesInStep_ = 0;
    static constexpr int kWarmupFrames = 20;

    StepResult initializeResult_;
    StepResult createResult_;
    StepResult achievementsResult_;

    int doneGraceFrames_ = -1;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
};
