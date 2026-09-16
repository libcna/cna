// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32_native_validation.md WINNATIVE-0010: what a Win32 test may assume about the
// desktop it is running on.
//
// Windows will not give a window a client area its monitor's work area cannot hold once the frame
// is added; it clamps, silently. So a test that writes down an absolute size and then asserts the
// size round-tripped is, without meaning to, asserting that the suite is running on a screen at
// least that big. That assumption is false in three places this project actually runs:
//
//   * the mingw-w64 cross-build under Wine, whose prefix has a 1024x768 virtual desktop at 192 DPI
//     -- a requested 1024x768 client comes back as 964x518, and so does a requested 800x600 one;
//   * a Windows SSH session, which lives in session 0 on a service window station that reports a
//     1024x768 display whatever the real monitor is;
//   * any small or high-DPI machine.
//
// Measured on Windows 10 build 19045: the same binary passed these tests on the 1920x1080
// interactive desktop and failed them in session 0, at the same moment, for no reason connected to
// what they are about. Deriving the size from the work area keeps the assertion exact -- the size
// still has to round-trip precisely -- while removing the hidden claim about the screen.

#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"

#include "Win32/Win32Common.hpp"

#include <algorithm>

namespace CNA::Platform::Testing
{
    /**
     * @brief The largest client size not exceeding the requested one that this desktop can give.
     *
     * @param desiredWidth Client width the test would prefer.
     * @param desiredHeight Client height the test would prefer.
     * @return The requested size, or the largest one the current work area can hold.
     */
    inline WindowSize SizeThatFitsTheWorkArea(const int desiredWidth, const int desiredHeight)
    {
        RECT workArea{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
            return WindowSize{desiredWidth, desiredHeight};

        // The frame's thickness depends on DPI and theme, so it is measured rather than assumed.
        RECT frame{0, 0, desiredWidth, desiredHeight};
        if (AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW) == FALSE)
            return WindowSize{desiredWidth, desiredHeight};
        const int frameWidth = (frame.right - frame.left) - desiredWidth;
        const int frameHeight = (frame.bottom - frame.top) - desiredHeight;

        const int availableWidth = (workArea.right - workArea.left) - frameWidth;
        const int availableHeight = (workArea.bottom - workArea.top) - frameHeight;

        // The floor keeps a pathologically small work area from producing a degenerate window and
        // turning a clear failure into a confusing one.
        return WindowSize{std::max(320, std::min(desiredWidth, availableWidth)),
                          std::max(240, std::min(desiredHeight, availableHeight))};
    }
}
