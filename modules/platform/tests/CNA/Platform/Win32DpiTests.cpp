// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0040: DPI reporting that is internally consistent.
//
// The failure this guards against is not a crash but a quietly wrong number: a display scale that
// disagrees with the drawable size makes a renderer build a swapchain of the wrong dimensions, and
// the result is a blurry or clipped game rather than an error anyone can see.
//
// The conversion arithmetic is tested directly; the live queries are tested for the properties
// that must hold whatever DPI awareness the host process happens to have.

#include "CNA/Platform/PlatformFactory.hpp"

#include "Win32/Win32Common.hpp"
#include "Win32/Win32DpiSupport.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Win32;

// --- the conversion ----------------------------------------------------------------------------

TEST(Win32Dpi, ScaleIsTheDpiRelativeTo96)
{
    EXPECT_FLOAT_EQ(Win32DpiSupport::ToDisplayScale(96), 1.0f);
    EXPECT_FLOAT_EQ(Win32DpiSupport::ToDisplayScale(120), 1.25f);
    EXPECT_FLOAT_EQ(Win32DpiSupport::ToDisplayScale(144), 1.5f);
    EXPECT_FLOAT_EQ(Win32DpiSupport::ToDisplayScale(192), 2.0f);
}

TEST(Win32Dpi, AZeroDpiNormalisesToOneRatherThanZero)
{
    // The contract says the scale is never zero, and it means it: a caller divides by this to
    // convert between logical and physical units, and zero divides to infinity rather than
    // failing anywhere a debugger would stop.
    EXPECT_FLOAT_EQ(Win32DpiSupport::ToDisplayScale(0), 1.0f);
}

// --- the live queries --------------------------------------------------------------------------

TEST(Win32Dpi, SystemDpiIsAlwaysAnswerable)
{
    // Three sources with fallbacks: GetDpiForSystem, then the monitor, then the device context.
    // At least one answers on every Windows version this backend supports.
    const unsigned int dpi = Win32DpiSupport::Get().GetSystemDpi();
    EXPECT_GT(dpi, 0u);
    EXPECT_GE(dpi, 48u) << "a plausible lower bound; below this something returned a stray value";
    EXPECT_LE(dpi, 960u) << "ten times the baseline is past any real display";
}

TEST(Win32Dpi, AdjustingAClientRectGrowsItByTheFrame)
{
    // The conversion every window creation and resize depends on. Getting it wrong loses the
    // frame and the title bar -- measured as 8x34 on a default decorated window.
    RECT frame{0, 0, 640, 480};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    ASSERT_TRUE(Win32DpiSupport::Get().AdjustWindowRect(frame, style, WS_EX_APPWINDOW, false, 96));

    EXPECT_LE(frame.left, 0) << "the frame extends left of the client origin";
    EXPECT_LT(frame.top, 0) << "the title bar sits above it";
    EXPECT_GT(frame.right - frame.left, 640);
    EXPECT_GT(frame.bottom - frame.top, 480);
}

TEST(Win32Dpi, AnUndecoratedWindowNeedsNoFrame)
{
    RECT frame{0, 0, 640, 480};
    ASSERT_TRUE(Win32DpiSupport::Get().AdjustWindowRect(frame, WS_POPUP, 0, false, 96));
    EXPECT_EQ(frame.right - frame.left, 640);
    EXPECT_EQ(frame.bottom - frame.top, 480);
}

TEST(Win32Dpi, AHigherDpiAsksForAProportionallyLargerFrame)
{
    // Only meaningful where AdjustWindowRectExForDpi exists; where it does not, the fallback
    // ignores the DPI and both answers are the system one. Both outcomes are correct, so the
    // assertion is "never smaller".
    RECT atBaseline{0, 0, 640, 480};
    RECT atDouble{0, 0, 640, 480};
    ASSERT_TRUE(
        Win32DpiSupport::Get().AdjustWindowRect(atBaseline, WS_OVERLAPPEDWINDOW, 0, false, 96));
    ASSERT_TRUE(
        Win32DpiSupport::Get().AdjustWindowRect(atDouble, WS_OVERLAPPEDWINDOW, 0, false, 192));
    EXPECT_GE(atDouble.bottom - atDouble.top, atBaseline.bottom - atBaseline.top);
}

// --- window coherence --------------------------------------------------------------------------

class Win32DpiWindow : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        platform_->AcquireSubsystem(PlatformSubsystem::Video);

        WindowDescription description;
        description.title = "dpi coherence";
        description.width = 640;
        description.height = 480;
        description.visible = false;
        description.highDpi = true;
        window_ = platform_->CreateWindow(description);
        ASSERT_NE(window_, nullptr);
        window_->Sync();
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
};

TEST_F(Win32DpiWindow, TheThreeReportedQuantitiesAgree)
{
    // The rule from plans/plan_win32.md section 9: whatever DPI awareness the host chose, the
    // client bounds are what layout uses, the pixel size is what a swapchain uses, and the scale
    // relates them. GetClientRect already answers in the units the window draws in, so the two
    // sizes coincide -- multiplying by the scale on top would double-apply it.
    const WindowBounds bounds = window_->GetClientBounds();
    const WindowSize pixels = window_->GetPixelSize();
    const float scale = window_->GetDisplayScale();

    EXPECT_GT(bounds.width, 0);
    EXPECT_GT(bounds.height, 0);
    EXPECT_EQ(pixels.width, bounds.width);
    EXPECT_EQ(pixels.height, bounds.height);
    EXPECT_GT(scale, 0.0f);
}

TEST_F(Win32DpiWindow, WindowDpiMatchesItsMonitor)
{
    const HWND hwnd = static_cast<HWND>(window_->GetNativeHandle().window);
    ASSERT_NE(hwnd, nullptr);

    const unsigned int windowDpi = Win32DpiSupport::Get().GetWindowDpi(hwnd);
    EXPECT_GT(windowDpi, 0u);
    EXPECT_FLOAT_EQ(Win32DpiSupport::ToDisplayScale(windowDpi), window_->GetDisplayScale());

    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    ASSERT_NE(monitor, nullptr);
    EXPECT_GT(Win32DpiSupport::Get().GetMonitorDpi(monitor), 0u);
}

TEST_F(Win32DpiWindow, ScaleStaysStableWhileTheWindowStaysOnOneMonitor)
{
    // A scale that changed between two reads would make every cached layout wrong without
    // anything reporting a DisplayScaleChanged event.
    const float first = window_->GetDisplayScale();
    window_->SetSize(800, 600);
    window_->Sync();
    EXPECT_FLOAT_EQ(window_->GetDisplayScale(), first);
}

TEST_F(Win32DpiWindow, TheDisplayServiceAgreesWithTheWindow)
{
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);

    DisplayInfo display{};
    if (!displays->TryGetDisplayForWindow(*window_, display))
        GTEST_SKIP() << "this host reports no display for the window";

    EXPECT_GT(display.width, 0);
    EXPECT_GT(display.height, 0);
    EXPECT_GT(display.contentScale, 0.0f);
    // Both derive from the same monitor, so a disagreement means one of the two lookups found a
    // different screen than the other.
    EXPECT_FLOAT_EQ(display.contentScale, window_->GetDisplayScale());
}

} // namespace
