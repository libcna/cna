// SPDX-License-Identifier: MS-PL
//
// PLAT-135: the frame budget.
//
// The link to a terminal has no knowable speed. A local pseudo-terminal absorbs megabytes; the
// same program over ssh on a bad connection may manage tens of kilobytes a second. So a fixed
// frame rate is the wrong contract, and the right one is a budget: when the terminal cannot keep
// up, frames are *dropped* rather than queued, because the write blocks and a queued frame drags
// the game's own frame rate down with it.
//
// The rate is measured rather than configured, which means there is no constant here to get wrong
// -- but it also means the tests must supply a rate, since no test can make a pseudo-terminal slow.

#include "../../../src/Terminal/TerminalFrameBudget.hpp"
#include "../../../src/Terminal/TerminalSurfacePresenter.hpp"

#include "CNA/Platform/PseudoTerminalHarness.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Terminal;

constexpr std::uint64_t kSecond = 1000000000ull;

TEST(TerminalFrameBudgetTest, EverythingIsAffordableUntilSomethingHasBeenMeasured)
{
    // Refusing here would drop the very first frame -- the one whose write establishes what the
    // link can actually do. A budget that starved itself of measurements would never lift.
    TerminalFrameBudget budget;
    EXPECT_EQ(budget.GetMeasuredBytesPerSecond(), 0.0);
    EXPECT_TRUE(budget.CanAfford(10 * 1000 * 1000, kSecond));
}

TEST(TerminalFrameBudgetTest, AFastWriteLeavesTheBudgetEffectivelyUnbounded)
{
    // The local-terminal case, which must stay the common case: a write that returns at once means
    // there is no constraint worth modelling, and the budget must not invent one.
    TerminalFrameBudget budget;
    budget.ObserveWrite(64 * 1024, 0);
    EXPECT_GT(budget.GetMeasuredBytesPerSecond(), 100.0 * 1000 * 1000);
    EXPECT_TRUE(budget.CanAfford(1000 * 1000, kSecond));
}

TEST(TerminalFrameBudgetTest, ASlowLinkIsBelievedAndThenEnforced)
{
    TerminalFrameBudget budget;
    // 100 KB that took a full second: 100 KB/s, a plausibly bad ssh link.
    budget.ObserveWrite(100 * 1024, kSecond);
    EXPECT_NEAR(budget.GetMeasuredBytesPerSecond(), 100.0 * 1024, 1.0);

    // One second of burst is affordable; appreciably more is not.
    EXPECT_TRUE(budget.CanAfford(50 * 1024, kSecond));
    EXPECT_FALSE(budget.CanAfford(500 * 1024, kSecond));
}

TEST(TerminalFrameBudgetTest, SpendingIsReplenishedOverTime)
{
    TerminalFrameBudget budget;
    budget.SetBytesPerSecondForTesting(1000.0);

    EXPECT_TRUE(budget.CanAfford(1000, kSecond)) << "one second of budget starts available";
    EXPECT_FALSE(budget.CanAfford(1000, kSecond)) << "and is now spent";
    EXPECT_TRUE(budget.CanAfford(1000, 2 * kSecond)) << "a second later it is back";
}

TEST(TerminalFrameBudgetTest, UnspentBudgetDoesNotAccumulateWithoutLimit)
{
    // Without a cap, a program that paused for a minute would be allowed a minute's worth of
    // output in one frame -- which is exactly the blocking write the budget exists to prevent.
    TerminalFrameBudget budget;
    budget.SetBytesPerSecondForTesting(1000.0);
    (void)budget.CanAfford(1, kSecond);

    EXPECT_FALSE(budget.CanAfford(10 * 1000, 60 * kSecond))
        << "sixty idle seconds must not buy sixty seconds of burst";
    EXPECT_TRUE(budget.CanAfford(900, 60 * kSecond));
}

TEST(TerminalFrameBudgetTest, OneSlowWriteDoesNotConvinceTheBudgetTheLinkCollapsed)
{
    // A scheduler hiccup or a moment of contention is not a degraded link. Smoothing is what stops
    // a single outlier from throttling a fast terminal for the frames that follow.
    TerminalFrameBudget budget;
    for (int i = 0; i < 5; ++i)
    {
        budget.ObserveWrite(1000 * 1000, kSecond / 100);  // 100 MB/s
    }
    const double fast = budget.GetMeasuredBytesPerSecond();

    budget.ObserveWrite(1000, kSecond);  // one 1 KB/s outlier
    EXPECT_EQ(budget.GetMeasuredBytesPerSecond(), fast)
        << "a single outlier is a minority of the window and must not move the median at all";
}

TEST(TerminalFrameBudgetTest, ALinkThatReallyDegradedIsBelievedWithinAFewFrames)
{
    // The other half of the same trade-off: smoothing must not be so heavy that a genuinely slow
    // link keeps being handed frames it cannot take.
    TerminalFrameBudget budget;
    for (int i = 0; i < 5; ++i)
    {
        budget.ObserveWrite(1000 * 1000, kSecond / 100);  // 100 MB/s
    }
    for (int i = 0; i < 3; ++i)
    {
        budget.ObserveWrite(10 * 1000, kSecond);  // 10 KB/s
    }
    // Three consecutive slow writes are a majority of a five-sample window, so the median has
    // already followed them. An exponential average with a weight light enough to ignore a single
    // outlier would still be reporting megabytes a second here.
    EXPECT_LT(budget.GetMeasuredBytesPerSecond(), 1000.0 * 1000);
}

// --- through the presenter -------------------------------------------------------------------

class TerminalBudgetPresenter : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!pty_.IsOpen())
        {
            GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
        }
    }

    /// A frame whose every pixel differs from the last, so the diff is always the whole grid.
    static std::vector<std::uint8_t> Noise(const int width, const int height, const int seed)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(height) * 4u);
        for (std::size_t at = 0; at < pixels.size(); at += 4)
        {
            const auto value = static_cast<std::uint8_t>((at * 7 + static_cast<std::size_t>(seed) * 53) % 251);
            pixels[at] = value;
            pixels[at + 1] = static_cast<std::uint8_t>(255 - value);
            pixels[at + 2] = value;
            pixels[at + 3] = 255;
        }
        return pixels;
    }

    static SurfaceFrame View(const std::vector<std::uint8_t>& pixels, const int width,
                             const int height)
    {
        SurfaceFrame frame;
        frame.pixels = pixels.data();
        frame.width = width;
        frame.height = height;
        return frame;
    }

    CNA::Platform::TestSupport::PseudoTerminal pty_{40, 20};

    /// Without this the tests below deadlock rather than fail: a pty's buffer is a few kilobytes,
    /// a truecolor full-screen frame is tens, and a write with no reader waits forever on the
    /// thread that would have done the reading.
    std::unique_ptr<CNA::Platform::TestSupport::PseudoTerminalDrain> drain_ =
        std::make_unique<CNA::Platform::TestSupport::PseudoTerminalDrain>(pty_.Controller());
};

TEST_F(TerminalBudgetPresenter, TheFirstFrameIsNeverDropped)
{
    // It is a full redraw, and the screen is not merely stale but *wrong*: dropping it would leave
    // the terminal showing nothing at all until something else forced a redraw.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 160, 100);
    presenter.SetBytesPerSecondForTesting(1.0);  // one byte a second

    const std::vector<std::uint8_t> pixels = Noise(160, 100, 0);
    presenter.Present(View(pixels, 160, 100));

    EXPECT_EQ(presenter.GetDroppedFrameCount(), 0);
    EXPECT_GT(presenter.GetLastFrameBytes().size(), 0u);
}

TEST_F(TerminalBudgetPresenter, FramesAreDroppedRatherThanQueuedWhenTheLinkCannotKeepUp)
{
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 160, 100);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    const std::vector<std::uint8_t> first = Noise(160, 100, 1);
    presenter.Present(View(first, 160, 100));  // full redraw, exempt

    presenter.SetBytesPerSecondForTesting(1.0);
    for (int frame = 2; frame < 8; ++frame)
    {
        const std::vector<std::uint8_t> pixels = Noise(160, 100, frame);
        presenter.Present(View(pixels, 160, 100));
    }

    EXPECT_GT(presenter.GetDroppedFrameCount(), 0)
        << "a one-byte-per-second link must not be handed full frames";
    EXPECT_TRUE(presenter.GetLastFrameBytes().empty()) << "a dropped frame writes nothing";
}

TEST_F(TerminalBudgetPresenter, ADroppedFrameIsNotLostBecauseTheNextDiffStillCoversIt)
{
    // The reason dropping is safe at all. The presenter leaves its record of what is on screen
    // untouched, so the next frame that is affordable redraws everything that changed since the
    // last frame actually drawn -- not merely since the dropped one.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 40, 20);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    const std::vector<std::uint8_t> blank = Noise(40, 20, 0);
    presenter.Present(View(blank, 40, 20));

    presenter.SetBytesPerSecondForTesting(1.0);
    const std::vector<std::uint8_t> changed = Noise(40, 20, 9);
    presenter.Present(View(changed, 40, 20));
    ASSERT_EQ(presenter.GetDroppedFrameCount(), 1);

    // Budget restored; the same content must still be sent, because it never reached the screen.
    presenter.SetBytesPerSecondForTesting(0.0);
    presenter.Present(View(changed, 40, 20));
    EXPECT_GT(presenter.GetLastRedrawnCellCount(), 0)
        << "the dropped frame's changes were forgotten instead of carried forward";
}

TEST_F(TerminalBudgetPresenter, AFastTerminalNeverDropsAnything)
{
    // The measured rate on a local pty is enormous, so the budget must never bind. A frame budget
    // that throttled a fast terminal would be strictly worse than not having one.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 40, 20);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    for (int frame = 0; frame < 30; ++frame)
    {
        const std::vector<std::uint8_t> pixels = Noise(40, 20, frame);
        presenter.Present(View(pixels, 40, 20));
    }
    EXPECT_EQ(presenter.GetDroppedFrameCount(), 0);
    EXPECT_GT(presenter.GetMeasuredBytesPerSecond(), 0.0) << "the writes should have been measured";
}

TEST_F(TerminalBudgetPresenter, AnUnchangedFrameCostsNothingAndIsNotCountedAsDropped)
{
    // A still picture emits no bytes, so there is nothing to charge for and nothing was lost.
    // Counting it as a drop would make the diagnostic lie about why the frame rate is low.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 40, 20);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    const std::vector<std::uint8_t> pixels = Noise(40, 20, 3);
    presenter.Present(View(pixels, 40, 20));

    presenter.SetBytesPerSecondForTesting(1.0);
    presenter.Present(View(pixels, 40, 20));
    EXPECT_EQ(presenter.GetDroppedFrameCount(), 0);
    EXPECT_EQ(presenter.GetLastRedrawnCellCount(), 0);
}

} // namespace
