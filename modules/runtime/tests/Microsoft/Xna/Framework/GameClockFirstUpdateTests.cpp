// SPDX-License-Identifier: MS-PL
// XNA 4.0's own game clock, measured against the real runtime under Wine and adopted over FNA's
// on the project owner's decision (SAMPLE-043 / SAMPLE-044):
//
//   fixed=True  update 1: elapsed=0.000000000 total=0.000000000
//   fixed=True  update 2: elapsed=0.016666700 total=0.000000000
//   fixed=True  update 3: elapsed=0.016666700 total=0.016666700
//
// Two rules: the game's FIRST update runs with a zero ElapsedGameTime, and TotalGameTime is the
// time BEFORE the step, so it advances once Update returns. FNA does neither -- it sets
// ElapsedGameTime = TargetElapsedTime for every update including the first and advances
// TotalGameTime before calling Update -- which left a CNA game two fixed steps ahead of XNA's by
// the same update index.

#include <vector>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;

namespace
{
    struct Step
    {
        double elapsed;
        double total;
    };

    /// Records the clock each update sees and exits once it has enough of them.
    class ClockRecordingGame : public Game
    {
    public:
        std::vector<Step> steps;

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name{"CnaTests.ClockRecordingGame"};
            return name;
        }

    protected:
        void Update(GameTime& gameTime) override
        {
            steps.push_back({gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty(),
                             gameTime.getTotalGameTimeProperty().getTotalSecondsProperty()});
            if (steps.size() >= 4)
                Exit();
            Game::Update(gameTime);
        }
    };

    TEST(GameClockFirstUpdateTest, TheFirstFixedStepUpdateSeesZeroElapsedTime)
    {
        ClockRecordingGame game;
        game.setIsFixedTimeStepProperty(true);
        game.Run();

        ASSERT_GE(game.steps.size(), 4u);
        const double step = game.getTargetElapsedTimeProperty().getTotalSecondsProperty();

        EXPECT_DOUBLE_EQ(0.0, game.steps[0].elapsed) << "XNA's first update has no elapsed time";
        EXPECT_DOUBLE_EQ(step, game.steps[1].elapsed);
        EXPECT_DOUBLE_EQ(step, game.steps[2].elapsed);
    }

    // The second half of the rule, and the one that actually moves a simulation: during update N
    // the total is the sum of the PREVIOUS updates' elapsed times, so it lags by one step.
    TEST(GameClockFirstUpdateTest, TotalGameTimeIsTheTimeBeforeTheStep)
    {
        ClockRecordingGame game;
        game.setIsFixedTimeStepProperty(true);
        game.Run();

        ASSERT_GE(game.steps.size(), 4u);
        const double step = game.getTargetElapsedTimeProperty().getTotalSecondsProperty();

        EXPECT_DOUBLE_EQ(0.0, game.steps[0].total);
        EXPECT_DOUBLE_EQ(0.0, game.steps[1].total) << "the first step contributed nothing";
        EXPECT_DOUBLE_EQ(step, game.steps[2].total);
        EXPECT_DOUBLE_EQ(2 * step, game.steps[3].total);
    }

    // The same rule in the variable path, where XNA reports update 3 with elapsed 0.0211 and total
    // still 0, then update 4 with total 0.0211.
    TEST(GameClockFirstUpdateTest, VariableStepTotalAlsoLagsItsOwnUpdate)
    {
        ClockRecordingGame game;
        game.setIsFixedTimeStepProperty(false);
        game.Run();

        ASSERT_GE(game.steps.size(), 4u);
        EXPECT_DOUBLE_EQ(0.0, game.steps[0].total);
        for (std::size_t i = 1; i < game.steps.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(game.steps[i - 1].total + game.steps[i - 1].elapsed,
                             game.steps[i].total)
                << "total at update " << (i + 1) << " must be the sum of the previous elapseds";
        }
    }
}
