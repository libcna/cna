// SPDX-License-Identifier: MS-PL
// Genuine XNA4 reference: cna-demos lifecycle/ResetElapsedTimeOracle.cs.
#include <gtest/gtest.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "RuntimePlatformTestSupport.hpp"

namespace {
using namespace Microsoft::Xna::Framework;

class ScriptedClockPlatform final : public CNA::Platform::Testing::PlatformTestDecorator {
public:
    ScriptedClockPlatform() : PlatformTestDecorator(CNA::Platform::PlatformFactory::Create()) {}
    std::uint64_t GetPerformanceCounter() const override { return counter_++; }
    std::uint64_t GetPerformanceFrequency() const override { return 10000; }
    void Delay(std::uint32_t milliseconds) override { Advance(milliseconds); }
    void Advance(std::uint32_t milliseconds) { counter_ += milliseconds * std::uint64_t{10}; }
private:
    // Each read advances 100us, so the fixed-step spin loop progresses without real sleeps.
    mutable std::uint64_t counter_ = 10000;
};

class TimingGame final : public Game {
public:
    explicit TimingGame(std::unique_ptr<CNA::Platform::IPlatform> platform) : Game(std::move(platform)) {}
    std::vector<GameTime> updates;
    std::vector<GameTime> draws;
    std::function<void(GameTime&)> onUpdate;
protected:
    void Update(GameTime& time) override {
        updates.push_back(time);
        if (onUpdate) onUpdate(time);
        Game::Update(time);
    }
    void Draw(const GameTime& time) override {
        draws.push_back(time);
        Game::Draw(time);
    }
};

class ResetElapsedTimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
            GTEST_SKIP() << "The selected platform cannot create a test window.";
        auto platform = std::make_unique<ScriptedClockPlatform>();
        clock = platform.get();
        game = std::make_unique<TimingGame>(std::move(platform));
        graphics = std::make_unique<GraphicsDeviceManager>(game.get());
        game->setTargetElapsedTimeProperty(System::TimeSpan::FromMilliseconds(10));
        game->RunOneFrame();
        ASSERT_EQ(game->updates.size(), 1U);
        game->updates.clear();
        game->draws.clear();
    }
    ScriptedClockPlatform* clock = nullptr;
    std::unique_ptr<TimingGame> game;
    std::unique_ptr<GraphicsDeviceManager> graphics;
};

TEST_F(ResetElapsedTimeTest, FixedResetConsumesStaleTimeWithoutCatchUp) {
    clock->Advance(200);
    game->ResetElapsedTime();
    game->RunOneFrame();
    EXPECT_TRUE(game->updates.empty());
    clock->Advance(10);
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_EQ(game->updates.back().getElapsedGameTimeProperty(), game->getTargetElapsedTimeProperty());
    EXPECT_FALSE(game->updates.back().getIsRunningSlowlyProperty());
}

TEST_F(ResetElapsedTimeTest, ResetInsideUpdateDoesNotCancelAlreadyAccumulatedSteps) {
    bool reset = false;
    game->onUpdate = [&](GameTime&) {
        if (!reset) { reset = true; game->ResetElapsedTime(); }
    };
    clock->Advance(30);
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 3U);
    clock->Advance(200);
    game->RunOneFrame();
    EXPECT_EQ(game->updates.size(), 3U);
}

TEST_F(ResetElapsedTimeTest, VariableResetZeroesExactlyOneUpdate) {
    game->setIsFixedTimeStepProperty(false);
    clock->Advance(200);
    game->ResetElapsedTime();
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_EQ(game->updates.back().getElapsedGameTimeProperty(), System::TimeSpan::Zero);
    clock->Advance(30);
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 2U);
    EXPECT_GT(game->updates.back().getElapsedGameTimeProperty().getTicksProperty(), 0);
}

TEST_F(ResetElapsedTimeTest, FixedResetPreservesFractionalAccumulatedTime) {
    clock->Advance(25);
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 2U);
    game->updates.clear();
    clock->Advance(200);
    game->ResetElapsedTime();
    game->RunOneFrame();
    EXPECT_TRUE(game->updates.empty());
    // Lowering the target exposes the retained five-millisecond remainder without
    // letting Tick's normal fixed-step wait supply the missing time.
    game->setTargetElapsedTimeProperty(System::TimeSpan::FromMilliseconds(5));
    game->ResetElapsedTime();
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_EQ(game->updates.back().getElapsedGameTimeProperty(), game->getTargetElapsedTimeProperty());
}

TEST_F(ResetElapsedTimeTest, ResetClearsPreviouslyObservedSlowHistory) {
    clock->Advance(70);
    game->RunOneFrame();
    clock->Advance(10);
    game->RunOneFrame();
    ASSERT_FALSE(game->updates.empty());
    ASSERT_TRUE(game->updates.back().getIsRunningSlowlyProperty());
    game->updates.clear();
    clock->Advance(200);
    game->ResetElapsedTime();
    game->RunOneFrame();
    EXPECT_TRUE(game->updates.empty());
    clock->Advance(10);
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_FALSE(game->updates.back().getIsRunningSlowlyProperty());
}

TEST_F(ResetElapsedTimeTest, RetainedDebtDoesNotImmediatelyRestoreSlowDrawHistory) {
    clock->Advance(29);
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 2U);
    game->updates.clear();
    game->draws.clear();
    game->setTargetElapsedTimeProperty(System::TimeSpan::FromMilliseconds(1));
    game->ResetElapsedTime();
    game->RunOneFrame();
    ASSERT_EQ(game->updates.size(), 9U);
    EXPECT_FALSE(game->updates.back().getIsRunningSlowlyProperty());
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_FALSE(game->draws.back().getIsRunningSlowlyProperty());
}

TEST_F(ResetElapsedTimeTest, ResetPreservesCurrentGameTimeAndClearsNextPlannedUpdateSlowFlag) {
    clock->Advance(70);
    game->RunOneFrame();
    game->updates.clear();
    bool beforeReset = false, afterReset = false, nextUpdate = true;
    game->onUpdate = [&](GameTime& time) {
        if (game->updates.size() == 1) {
            beforeReset = time.getIsRunningSlowlyProperty();
            game->ResetElapsedTime();
            afterReset = time.getIsRunningSlowlyProperty();
        } else if (game->updates.size() == 2) nextUpdate = time.getIsRunningSlowlyProperty();
    };
    clock->Advance(30);
    game->RunOneFrame();
    EXPECT_EQ(game->updates.size(), 3U);
    EXPECT_TRUE(beforeReset);
    EXPECT_TRUE(afterReset);
    EXPECT_FALSE(nextUpdate);
}
} // namespace
