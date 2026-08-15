// SPDX-License-Identifier: MS-PL
//
// PLAT-136: the terminal changed size, and the game finds out.
//
// SIGWINCH is the only announcement a terminal makes, and it carries nothing but itself — no new
// size, no queue to drain. So the handler does the one thing a signal handler safely can, and the
// platform's ordinary per-frame poll turns that into the same WindowEvent every other platform
// emits. That is the whole point of the row: GameWindow and GraphicsDeviceManager get a resize
// they already know how to handle, with no terminal-specific code anywhere above this layer.
//
// The signal itself cannot be provoked in a test. SIGWINCH goes to the foreground process group of
// the *controlling* terminal, and a test process's pseudo-terminal is not that, so resizing it
// delivers nothing. SimulateResizeForTesting() sets the same flag the handler sets, which leaves
// exactly one link untested — that sigaction() was called with the right signal number — and
// covers every line that turns a flag into an event.

#include "../../../src/Terminal/TerminalResizeWatcher.hpp"

#include "CNA/Platform/PseudoTerminalHarness.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <signal.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Terminal;

TEST(TerminalResizeWatcherTest, APendingResizeIsReportedOnceAndThenConsumed)
{
    // Consumed, not levelled: a flag that stayed set would make every subsequent frame emit a
    // resize event for a terminal that stopped changing size long ago.
    const TerminalResizeWatcher watcher;
    EXPECT_FALSE(TerminalResizeWatcher::TakePendingResize());

    TerminalResizeWatcher::SimulateResizeForTesting();
    EXPECT_TRUE(TerminalResizeWatcher::TakePendingResize());
    EXPECT_FALSE(TerminalResizeWatcher::TakePendingResize());
}

TEST(TerminalResizeWatcherTest, ManyResizesBetweenTwoPollsCoalesceIntoOne)
{
    // Coalescing is free and correct, not a shortcut: what a caller wants is the terminal's
    // *current* size, and ten resizes between two frames have one current size. A queue would
    // deliver nine events describing sizes that no longer exist.
    const TerminalResizeWatcher watcher;
    for (int i = 0; i < 10; ++i)
    {
        TerminalResizeWatcher::SimulateResizeForTesting();
    }
    EXPECT_TRUE(TerminalResizeWatcher::TakePendingResize());
    EXPECT_FALSE(TerminalResizeWatcher::TakePendingResize());
}

TEST(TerminalResizeWatcherTest, ASecondWatcherIsRefused)
{
    // The handler is process-global, so a second watcher would overwrite the first's saved
    // disposition and leave SIGWINCH pointing at a handler whose owner is gone.
    const TerminalResizeWatcher first;
    EXPECT_TRUE(TerminalResizeWatcher::IsWatching());
    EXPECT_THROW((void)TerminalResizeWatcher(), PlatformException);
}

TEST(TerminalResizeWatcherTest, TheHandlerIsRemovedWhenTheWatcherGoesAway)
{
    // Otherwise the next SIGWINCH lands in a handler belonging to nothing, and any host
    // application that had its own handler never gets it back.
    struct sigaction before = {};
    ASSERT_EQ(sigaction(SIGWINCH, nullptr, &before), 0);

    {
        const TerminalResizeWatcher watcher;
        struct sigaction during = {};
        ASSERT_EQ(sigaction(SIGWINCH, nullptr, &during), 0);
        EXPECT_NE(during.sa_handler, before.sa_handler) << "the watcher never installed anything";
    }

    struct sigaction after = {};
    ASSERT_EQ(sigaction(SIGWINCH, nullptr, &after), 0);
    EXPECT_EQ(after.sa_handler, before.sa_handler);
    EXPECT_FALSE(TerminalResizeWatcher::IsWatching());
}

TEST(TerminalResizeWatcherTest, ARealSignalSetsTheFlag)
{
    // The one link SimulateResizeForTesting cannot cover: that sigaction() was given SIGWINCH and
    // not some neighbouring number. Raising it in this process is safe -- SIGWINCH's default
    // disposition is to be ignored, so a mistake here cannot kill the test binary.
    const TerminalResizeWatcher watcher;
    (void)TerminalResizeWatcher::TakePendingResize();

    ASSERT_EQ(raise(SIGWINCH), 0);
    EXPECT_TRUE(TerminalResizeWatcher::TakePendingResize()) << "SIGWINCH did not reach the handler";
}

// --- through the platform -----------------------------------------------------------------------

const char* ResizeHarnessPath()
{
#if defined(CNA_PLATFORM_TERMINAL_RESIZE_HARNESS_PATH)
    return CNA_PLATFORM_TERMINAL_RESIZE_HARNESS_PATH;
#else
    return nullptr;
#endif
}

std::string ExplainHarnessCode(const int code)
{
    switch (code)
    {
        case 10: return "the harness was not given a terminal";
        case 11: return "the platform reported no surface presentation despite having a terminal";
        case 12: return "polling before any resize produced events";
        case 13: return "a resize did not produce exactly the two expected window events";
        case 14: return "the events were not Resized then PixelSizeChanged for this window";
        case 15: return "the reported size was not positive, or the two events disagreed";
        case 16: return "the window did not adopt the size the event announced";
        default: return "unknown harness failure";
    }
}

TEST(TerminalResizeTest, TheWholeResizePathHoldsWhereARealTerminalExists)
{
    // Run in a spawned process because this one has no terminal: CI redirects output, so the
    // watcher is never installed and every assertion below would skip. A test that always skips
    // proves nothing -- this campaign has already been bitten by exactly that once, by 21
    // conformance tests that matched no filter and stayed green for years.
    //
    // The harness asserts the whole row: that a resize becomes WindowEventKind::Resized followed
    // by PixelSizeChanged, both naming this window, both carrying the same positive size, and that
    // the window adopts that size at once rather than waiting for Sync(). Its exit code says which
    // of those failed.
    const char* path = ResizeHarnessPath();
    if (path == nullptr)
    {
        GTEST_SKIP() << "the resize harness was not built in this configuration";
    }

    const CNA::Platform::TestSupport::HarnessRun run =
        CNA::Platform::TestSupport::RunUnderPseudoTerminal(path, "", 100, 30);
    ASSERT_TRUE(run.spawned);
    ASSERT_TRUE(WIFEXITED(run.status)) << "the harness died rather than reporting";
    EXPECT_EQ(WEXITSTATUS(run.status), 0) << ExplainHarnessCode(WEXITSTATUS(run.status));
}

TEST(TerminalResizeTest, NoWatcherIsInstalledWhenThereIsNoWindowToResize)
{
    // Installing a process-global signal handler merely because a platform exists would be state
    // a constructor has no business changing -- and the conformance suite constructs platforms in
    // a process whose signal dispositions belong to somebody else.
    const std::unique_ptr<IPlatform> platform = PlatformFactory::Create("Terminal");
    EXPECT_FALSE(TerminalResizeWatcher::IsWatching());
}

TEST(TerminalResizeTest, PollingIsHarmlessWhenNothingIsAttached)
{
    // The common case in CI: output redirected, so no watcher, no window, no events -- and
    // certainly no crash reading a window that was never made.
    const std::unique_ptr<IPlatform> platform = PlatformFactory::Create("Terminal");
    std::vector<PlatformEvent> batch;
    EXPECT_NO_THROW(platform->PollEvents(batch));
    EXPECT_TRUE(batch.empty());
}

} // namespace
