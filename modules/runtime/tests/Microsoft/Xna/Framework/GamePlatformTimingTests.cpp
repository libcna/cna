// SPDX-License-Identifier: MS-PL
//
// PLAT-48 / PLAT-49: the game loop's timing and cursor handling go through the platform.
//
// Timing is the one dependency the fixed-timestep loop cannot do without: a counter that does not
// advance makes Tick() spin forever inside its own sleep-until-target loop, and a frequency
// reported as zero divides by zero in AdvanceElapsedTime. Neither shows up as a compile error
// after the migration, and the second one only fires on a platform whose counter is scaled
// differently -- which is precisely what a second implementation will be.
//
// The observable behaviour of the loop itself is already pinned by PLAT-6's transcript. What is
// left to check here is that the loop is reading the platform at all, and that the platform it
// reads is fit for the job.

#include <gtest/gtest.h>

#include "CNA/Platform/IPlatform.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/TimeSpan.hpp"

#include <cstdint>

using namespace Microsoft::Xna::Framework;

namespace
{
    class TimingGame : public Game
    {
    public:
        int updateCalls = 0;

    protected:
        void LoadContent() override {}
        void Update(GameTime& gameTime) override
        {
            ++updateCalls;
            Game::Update(gameTime);
        }
        void Draw(const GameTime& gameTime) override { Game::Draw(gameTime); }
    };
}

TEST(GamePlatformTimingTest, ThePlatformCounterIsMonotonic)
{
    // Elapsed time is computed as an unsigned difference, so a counter that ever goes backwards
    // produces an enormous positive delta rather than a negative one -- a single frame that
    // appears to have taken years, which the loop then clamps to MaxElapsedTime and hides.
    TimingGame game;
    CNA::Platform::IPlatform& platform = game.GetPlatformEXT();

    std::uint64_t previous = platform.GetPerformanceCounter();
    for (int i = 0; i < 200; ++i)
    {
        const std::uint64_t current = platform.GetPerformanceCounter();
        ASSERT_GE(current, previous) << "iteration " << i;
        previous = current;
    }
}

TEST(GamePlatformTimingTest, ThePlatformFrequencyIsNonZeroAndStable)
{
    // AdvanceElapsedTime divides by this. Zero is undefined behaviour, and a value that changes
    // between calls would make two frames of the same real duration report different elapsed
    // times.
    TimingGame game;
    CNA::Platform::IPlatform& platform = game.GetPlatformEXT();

    const std::uint64_t frequency = platform.GetPerformanceFrequency();
    EXPECT_GT(frequency, 0u);
    EXPECT_EQ(frequency, platform.GetPerformanceFrequency());
}

TEST(GamePlatformTimingTest, TheCounterActuallyAdvancesOverARealDelay)
{
    // A counter that is monotonic but constant satisfies the test above and still hangs the
    // fixed-timestep loop, which waits for accumulated time to reach the target.
    TimingGame game;
    CNA::Platform::IPlatform& platform = game.GetPlatformEXT();

    const std::uint64_t before = platform.GetPerformanceCounter();
    platform.Delay(5);
    const std::uint64_t after = platform.GetPerformanceCounter();

    EXPECT_GT(after, before);
}

TEST(GamePlatformTimingTest, MillisecondTicksAdvanceOverARealDelay)
{
    // The Emscripten main-loop callback drives its accumulator from this rather than from the
    // performance counter, so it needs its own check.
    TimingGame game;
    CNA::Platform::IPlatform& platform = game.GetPlatformEXT();

    const std::uint64_t before = platform.GetTicksMilliseconds();
    platform.Delay(5);
    EXPECT_GE(platform.GetTicksMilliseconds(), before + 1);
}

TEST(GamePlatformTimingTest, AFixedTimestepFrameAdvancesGameTime)
{
    // The end-to-end check that the loop is reading the platform's clock: with a fixed timestep
    // the frame must accumulate at least one step, which only happens if elapsed time advances.
    TimingGame game;
    GraphicsDeviceManager gdm(&game);
    game.setIsFixedTimeStepProperty(true);
    game.setTargetElapsedTimeProperty(System::TimeSpan::FromMilliseconds(1.0));

    ASSERT_NO_THROW(game.RunOneFrame());
    ASSERT_NO_THROW(game.RunOneFrame());

    EXPECT_GE(game.updateCalls, 1);
}

TEST(GamePlatformTimingTest, AVariableTimestepFrameAlsoRuns)
{
    // The variable-timestep path takes a different route through AdvanceElapsedTime and would
    // keep working with a broken frequency, so it is worth its own frame.
    TimingGame game;
    GraphicsDeviceManager gdm(&game);
    game.setIsFixedTimeStepProperty(false);

    ASSERT_NO_THROW(game.RunOneFrame());
    EXPECT_GE(game.updateCalls, 1);
}

// --- PLAT-49: cursor visibility ---------------------------------------------------------------

TEST(GamePlatformTimingTest, MouseVisibilityRoundTripsThroughTheProperty)
{
    TimingGame game;

    game.setIsMouseVisibleProperty(true);
    EXPECT_TRUE(game.getIsMouseVisibleProperty());

    game.setIsMouseVisibleProperty(false);
    EXPECT_FALSE(game.getIsMouseVisibleProperty());
}

TEST(GamePlatformTimingTest, SettingMouseVisibilityIsSafeWithNoPointerService)
{
    // The property must work regardless of whether the platform has a mouse at all: a platform
    // that reports no pointer returns null from GetMouse(), and the previous native cursor call
    // had no such branch to get wrong. Headless and, later, terminal both take this path.
    TimingGame game;

    EXPECT_NO_THROW(game.setIsMouseVisibleProperty(true));
    EXPECT_NO_THROW(game.setIsMouseVisibleProperty(false));
    EXPECT_NO_THROW(game.setIsMouseVisibleProperty(false));  // repeat: the setter early-outs
}

TEST(GamePlatformTimingTest, MouseVisibilitySurvivesAFrame)
{
    TimingGame game;
    GraphicsDeviceManager gdm(&game);

    game.setIsMouseVisibleProperty(true);
    ASSERT_NO_THROW(game.RunOneFrame());
    EXPECT_TRUE(game.getIsMouseVisibleProperty());
}
