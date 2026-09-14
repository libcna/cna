// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0042: the windowed appearance survives repeated fullscreen cycles.
//
// Win32 has no fullscreen *mode* the system remembers. Going fullscreen means editing the window's
// style, extended style and placement in place, and anything not recorded before the edit is gone
// for good. The symptoms are all familiar: a window that comes back without its title bar, or
// maximised when it was not, or at the wrong size on the wrong monitor.
//
// Driven here without a window manager so the round trip is asserted on the state itself rather
// than on what a compositor happened to do with it.

#include "Win32/Win32Common.hpp"
#include "Win32/Win32FullscreenState.hpp"

#include <gtest/gtest.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Win32;

WINDOWPLACEMENT MakePlacement(const int left, const int top, const int right, const int bottom,
                              const UINT showCommand = SW_SHOWNORMAL)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    placement.showCmd = showCommand;
    placement.rcNormalPosition = RECT{left, top, right, bottom};
    return placement;
}

constexpr LONG_PTR kDecoratedStyle =
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;

// --- capture and restore -------------------------------------------------------------------------

TEST(Win32FullscreenState, StartsWithNothingCaptured)
{
    Win32FullscreenState state;
    EXPECT_FALSE(state.IsCaptured());

    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    WINDOWPLACEMENT placement{};
    EXPECT_FALSE(state.Restore(style, exStyle, placement));
}

TEST(Win32FullscreenState, RestoresEveryPieceItCaptured)
{
    Win32FullscreenState state;
    const WINDOWPLACEMENT original = MakePlacement(100, 120, 900, 720);
    state.Capture(kDecoratedStyle, WS_EX_APPWINDOW, original);
    EXPECT_TRUE(state.IsCaptured());

    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    WINDOWPLACEMENT placement{};
    ASSERT_TRUE(state.Restore(style, exStyle, placement));
    EXPECT_EQ(style, kDecoratedStyle);
    EXPECT_EQ(exStyle, static_cast<LONG_PTR>(WS_EX_APPWINDOW));
    EXPECT_EQ(placement.rcNormalPosition.left, original.rcNormalPosition.left);
    EXPECT_EQ(placement.rcNormalPosition.top, original.rcNormalPosition.top);
    EXPECT_EQ(placement.rcNormalPosition.right, original.rcNormalPosition.right);
    EXPECT_EQ(placement.rcNormalPosition.bottom, original.rcNormalPosition.bottom);
    EXPECT_EQ(placement.showCmd, original.showCmd);

    // Restoring consumes the recording: the window is windowed again and there is nothing left
    // to restore to.
    EXPECT_FALSE(state.IsCaptured());
}

TEST(Win32FullscreenState, PreservesAMaximisedWindowsShowCommand)
{
    // The placement is captured whole, not just its rectangle, so a window that was maximised
    // before going fullscreen comes back maximised rather than merely the right size.
    Win32FullscreenState state;
    state.Capture(kDecoratedStyle, 0, MakePlacement(0, 0, 1280, 720, SW_SHOWMAXIMIZED));

    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    WINDOWPLACEMENT placement{};
    ASSERT_TRUE(state.Restore(style, exStyle, placement));
    EXPECT_EQ(placement.showCmd, static_cast<UINT>(SW_SHOWMAXIMIZED));
}

TEST(Win32FullscreenState, ASecondCaptureDoesNotOverwriteTheGenuineWindowedState)
{
    // The bug this prevents: a second SetFullscreenMode(BorderlessFullscreen) captures the
    // ALREADY-fullscreen style, and the window can never be restored afterwards. Calling it twice
    // has to be harmless, because a game that sets its preferred mode on every settings change
    // will do exactly that.
    Win32FullscreenState state;
    state.Capture(kDecoratedStyle, WS_EX_APPWINDOW, MakePlacement(10, 20, 810, 620));

    const LONG_PTR fullscreenStyle = Win32FullscreenState::ToFullscreenStyle(kDecoratedStyle);
    state.Capture(fullscreenStyle, 0, MakePlacement(0, 0, 1920, 1080));

    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    WINDOWPLACEMENT placement{};
    ASSERT_TRUE(state.Restore(style, exStyle, placement));
    EXPECT_EQ(style, kDecoratedStyle) << "the windowed style, not the fullscreen one";
    EXPECT_EQ(placement.rcNormalPosition.right, 810);
}

TEST(Win32FullscreenState, SurvivesRepeatedWindowedBorderlessWindowedCycles)
{
    // The acceptance criterion stated in plans/plan_win32.md section 8: repeated transitions must
    // be idempotent. A leak of one decoration bit per cycle is invisible once and obvious after
    // twenty.
    Win32FullscreenState state;
    LONG_PTR style = kDecoratedStyle;
    LONG_PTR exStyle = WS_EX_APPWINDOW;
    WINDOWPLACEMENT placement = MakePlacement(64, 48, 864, 648);

    for (int cycle = 0; cycle < 20; ++cycle)
    {
        state.Capture(style, exStyle, placement);
        style = Win32FullscreenState::ToFullscreenStyle(style);
        exStyle = Win32FullscreenState::ToFullscreenExStyle(exStyle);

        ASSERT_TRUE(state.Restore(style, exStyle, placement)) << "cycle " << cycle;
        ASSERT_EQ(style, kDecoratedStyle) << "cycle " << cycle;
        ASSERT_EQ(exStyle, static_cast<LONG_PTR>(WS_EX_APPWINDOW)) << "cycle " << cycle;
        ASSERT_EQ(placement.rcNormalPosition.left, 64) << "cycle " << cycle;
        ASSERT_EQ(placement.rcNormalPosition.right, 864) << "cycle " << cycle;
    }
}

// --- the fullscreen styles -------------------------------------------------------------------------

TEST(Win32FullscreenState, FullscreenStyleRemovesEveryFramePiece)
{
    const LONG_PTR fullscreen = Win32FullscreenState::ToFullscreenStyle(kDecoratedStyle);
    EXPECT_EQ(fullscreen & WS_CAPTION, 0) << "a caption would inset the image and show a title bar";
    EXPECT_EQ(fullscreen & WS_THICKFRAME, 0);
    EXPECT_EQ(fullscreen & WS_SYSMENU, 0);
    EXPECT_EQ(fullscreen & WS_MINIMIZEBOX, 0);
    EXPECT_EQ(fullscreen & WS_MAXIMIZEBOX, 0);
    EXPECT_NE(fullscreen & WS_POPUP, 0) << "a borderless window is a popup";
}

TEST(Win32FullscreenState, FullscreenStylePreservesUnrelatedBits)
{
    // WS_CLIPCHILDREN and WS_CLIPSIBLINGS are drawing behaviour, not decoration. Stripping them
    // along with the frame would change how the window composites for the whole fullscreen
    // session -- and, because the style is restored verbatim, only while fullscreen.
    const LONG_PTR style = kDecoratedStyle | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    const LONG_PTR fullscreen = Win32FullscreenState::ToFullscreenStyle(style);
    EXPECT_NE(fullscreen & WS_CLIPCHILDREN, 0);
    EXPECT_NE(fullscreen & WS_CLIPSIBLINGS, 0);
}

TEST(Win32FullscreenState, FullscreenExStyleRemovesTheClientEdges)
{
    const LONG_PTR exStyle =
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    const LONG_PTR fullscreen = Win32FullscreenState::ToFullscreenExStyle(exStyle);
    EXPECT_EQ(fullscreen & WS_EX_WINDOWEDGE, 0);
    EXPECT_EQ(fullscreen & WS_EX_CLIENTEDGE, 0);
    EXPECT_EQ(fullscreen & WS_EX_STATICEDGE, 0);
    EXPECT_NE(fullscreen & WS_EX_APPWINDOW, 0) << "taskbar presence is not a frame decoration";
}

TEST(Win32FullscreenState, FullscreenStyleIsIdempotent)
{
    // Applying it to an already-fullscreen style must not keep changing the answer, because the
    // transition code may compute it more than once.
    const LONG_PTR once = Win32FullscreenState::ToFullscreenStyle(kDecoratedStyle);
    EXPECT_EQ(Win32FullscreenState::ToFullscreenStyle(once), once);
    const LONG_PTR exOnce = Win32FullscreenState::ToFullscreenExStyle(WS_EX_CLIENTEDGE);
    EXPECT_EQ(Win32FullscreenState::ToFullscreenExStyle(exOnce), exOnce);
}

} // namespace
