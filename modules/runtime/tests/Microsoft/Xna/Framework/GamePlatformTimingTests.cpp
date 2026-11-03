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
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

#include <cstdint>
#include <memory>
#include <vector>
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

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

TEST(GamePlatformTimingTest, TargetElapsedTimeRefusesZeroAndNegativeAsXnaDoes)
{
    // cna-killer KF-15: XNA's setter throws ArgumentOutOfRangeException("value", ...), which a
    // game catches as ArgumentException; CNA threw std::out_of_range.
    TimingGame game;
    const System::TimeSpan before = game.getTargetElapsedTimeProperty();
    EXPECT_THROW(game.setTargetElapsedTimeProperty(System::TimeSpan::Zero),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(game.setTargetElapsedTimeProperty(System::TimeSpan::FromMilliseconds(-1.0)),
                 System::ArgumentOutOfRangeException);
    EXPECT_EQ(game.getTargetElapsedTimeProperty(), before);
    game.setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(1));
    EXPECT_EQ(game.getTargetElapsedTimeProperty(), System::TimeSpan::FromTicks(1));
}

TEST(GamePlatformTimingTest, InactiveSleepTimeRefusesNegativeButAcceptsZeroAsXnaDoes)
{
    TimingGame game;
    EXPECT_THROW(game.setInactiveSleepTimeProperty(System::TimeSpan::FromTicks(-1)),
                 System::ArgumentOutOfRangeException);
    game.setInactiveSleepTimeProperty(System::TimeSpan::Zero);
    EXPECT_EQ(game.getInactiveSleepTimeProperty(), System::TimeSpan::Zero);
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

namespace {
    // Exact 100 ns clock: reads do not advance time. Tests advance before each fixed Tick,
    // so no wall-clock sleeps or host scheduling enter their expected arithmetic.
    class ExactClockPlatform final : public CNA::Platform::Testing::PlatformTestDecorator {
    public:
        ExactClockPlatform() : PlatformTestDecorator(CNA::Platform::PlatformFactory::Create()) {}
        std::uint64_t GetPerformanceCounter() const override { return ticks; }
        std::uint64_t GetPerformanceFrequency() const override { return 10000000; }
        void Delay(std::uint32_t milliseconds) override { ticks += milliseconds * std::uint64_t{10000}; }
        std::uint64_t ticks = 10000000;
    };

    class ExactClockGame final : public Game {
    public:
        explicit ExactClockGame(std::unique_ptr<CNA::Platform::IPlatform> platform) : Game(std::move(platform)) {}
        std::vector<GameTime> updates, draws;
        bool suppress = false;
    protected:
        bool BeginDraw() override { return true; }
        void EndDraw() override {}
        void Update(GameTime& time) override {
            updates.push_back(time);
            if (suppress) SuppressDraw();
        }
        void Draw(const GameTime& time) override { draws.push_back(time); }
    };

    class ExactGameClockTest : public ::testing::Test {
    protected:
        void SetUp() override {
            auto platform = std::make_unique<ExactClockPlatform>();
            clock = platform.get();
            game = std::make_unique<ExactClockGame>(std::move(platform));
            game->setIsFixedTimeStepProperty(false);
            game->setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(100000));
            game->RunOneFrame(); // initialization and zero-time update; no debt
            ASSERT_EQ(game->updates.size(), 1U);
            ASSERT_EQ(game->updates[0].getTotalGameTimeProperty().getTicksProperty(), 0);
            ASSERT_EQ(game->updates[0].getElapsedGameTimeProperty().getTicksProperty(), 0);
            Clear();
        }
        void Clear() { game->updates.clear(); game->draws.clear(); }
        void Advance(std::uint64_t ticks) { clock->ticks += ticks; game->RunOneFrame(); }
        ExactClockPlatform* clock = nullptr;
        std::unique_ptr<ExactClockGame> game;
    };
}

TEST_F(ExactGameClockTest, FixedStepsRetainRemainderAndDrawTheirCombinedElapsedTime) {
    // XNA Game.Tick IL_00b1..00d1 divides accumulated ticks by target; IL_01b4..01e8
    // subtracts one target per update and sums draw elapsed. 25ms => 2*10ms + 5ms.
    game->setIsFixedTimeStepProperty(true);
    Advance(250000);
    ASSERT_EQ(game->updates.size(), 2U);
    EXPECT_EQ(game->updates[0].getTotalGameTimeProperty().getTicksProperty(), 0);
    EXPECT_EQ(game->updates[1].getTotalGameTimeProperty().getTicksProperty(), 100000);
    for (const auto& time : game->updates)
        EXPECT_EQ(time.getElapsedGameTimeProperty().getTicksProperty(), 100000);
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_EQ(game->draws[0].getElapsedGameTimeProperty().getTicksProperty(), 200000);
    EXPECT_EQ(game->draws[0].getTotalGameTimeProperty().getTicksProperty(), 200000);
    Clear();
    Advance(150000); // retained 5ms + 15ms => two more updates, no remainder
    ASSERT_EQ(game->updates.size(), 2U);
    EXPECT_EQ(game->updates[0].getTotalGameTimeProperty().getTicksProperty(), 200000);
    EXPECT_EQ(game->updates[1].getTotalGameTimeProperty().getTicksProperty(), 300000);
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_EQ(game->draws[0].getElapsedGameTimeProperty().getTicksProperty(), 200000);
    EXPECT_EQ(game->draws[0].getTotalGameTimeProperty().getTicksProperty(), 400000);
}

TEST_F(ExactGameClockTest, VariableClampDropsExcessAndNextFrameUsesOnlyFreshTime) {
    // XNA Tick IL_0063..0077 clamps one elapsed sample to 500ms; IL_0239..029a
    // supplies pre-update total, then advances it. The dropped 300ms never becomes debt.
    Advance(8000000);
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_EQ(game->updates[0].getElapsedGameTimeProperty().getTicksProperty(), 5000000);
    EXPECT_EQ(game->updates[0].getTotalGameTimeProperty().getTicksProperty(), 0);
    EXPECT_FALSE(game->updates[0].getIsRunningSlowlyProperty());
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_EQ(game->draws[0].getTotalGameTimeProperty().getTicksProperty(), 5000000);
    Clear();
    Advance(30000);
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_EQ(game->updates[0].getElapsedGameTimeProperty().getTicksProperty(), 30000);
    EXPECT_EQ(game->updates[0].getTotalGameTimeProperty().getTicksProperty(), 5000000);
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_EQ(game->draws[0].getTotalGameTimeProperty().getTicksProperty(), 5030000);
}

TEST_F(ExactGameClockTest, FixedClampLimitsCatchUpToFiftyTenMillisecondUpdates) {
    // XNA maximumElapsedTime=500ms; 500ms / 10ms = 50, with initially zero debt.
    game->setIsFixedTimeStepProperty(true);
    Advance(8000000);
    ASSERT_EQ(game->updates.size(), 50U);
    for (std::size_t i = 0; i < game->updates.size(); ++i) {
        EXPECT_EQ(game->updates[i].getElapsedGameTimeProperty().getTicksProperty(), 100000);
        EXPECT_EQ(game->updates[i].getTotalGameTimeProperty().getTicksProperty(), i * 100000);
    }
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_EQ(game->draws[0].getElapsedGameTimeProperty().getTicksProperty(), 5000000);
    Clear();
    Advance(100000);
    ASSERT_EQ(game->updates.size(), 1U);
    EXPECT_EQ(game->updates[0].getTotalGameTimeProperty().getTicksProperty(), 5000000);
}

TEST_F(ExactGameClockTest, SuppressingEveryCatchUpUpdateSkipsOnlyThatDraw) {
    // XNA Tick IL_01a2..01ad consumes SuppressDraw per update; all three suppress here.
    game->setIsFixedTimeStepProperty(true);
    game->suppress = true;
    Advance(300000);
    ASSERT_EQ(game->updates.size(), 3U);
    EXPECT_TRUE(game->draws.empty());
    game->suppress = false;
    Clear();
    Advance(100000);
    ASSERT_EQ(game->updates.size(), 1U);
    ASSERT_EQ(game->draws.size(), 1U);
    EXPECT_EQ(game->draws[0].getElapsedGameTimeProperty().getTicksProperty(), 100000);
    EXPECT_EQ(game->draws[0].getTotalGameTimeProperty().getTicksProperty(), 400000);
}
