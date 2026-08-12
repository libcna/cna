// SPDX-License-Identifier: MS-PL
//
// PLAT-30/31/32: Sdl3Window against real SDL3.
//
// Every test here needs a live video subsystem, which a container without a display server may
// not be able to provide. They skip rather than fail in that case: a missing display is an
// environment fact, not a defect, and a test that fails on it teaches nothing.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class Sdl3WindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }

        platform_ = PlatformFactory::Create("SDL3");
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "no video subsystem available here: " << error.what();
        }
        acquiredVideo_ = true;

        WindowDescription description;
        description.title = "CNA platform test";
        description.width = 320;
        description.height = 240;
        description.visible = false;  // never steal focus from a developer's desktop
        try
        {
            window_ = platform_->CreateWindow(description);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "window creation unavailable here: " << error.what();
        }
        ASSERT_NE(window_, nullptr);
    }

    void TearDown() override
    {
        window_.reset();
        if (acquiredVideo_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    bool acquiredVideo_ = false;
};

// --- creation (PLAT-30) --------------------------------------------------------------------------

TEST_F(Sdl3WindowTest, CreatedWindowHasANonZeroIdAndTheRequestedTitle)
{
    EXPECT_NE(window_->GetId(), 0u);
    EXPECT_EQ(window_->GetTitle(), "CNA platform test");
}

TEST_F(Sdl3WindowTest, CreatedWindowHasTheRequestedSize)
{
    const WindowBounds bounds = window_->GetClientBounds();
    EXPECT_EQ(bounds.width, 320);
    EXPECT_EQ(bounds.height, 240);
}

TEST_F(Sdl3WindowTest, WindowIdsAreDistinctAcrossWindows)
{
    // PlatformEvent routes by window id, so two live windows sharing one would misdeliver every
    // event on the second.
    WindowDescription description;
    description.title = "second";
    description.visible = false;
    const std::unique_ptr<IPlatformWindow> second = platform_->CreateWindow(description);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->GetId(), window_->GetId());
}

// --- geometry and state (PLAT-31) -----------------------------------------------------------------

TEST_F(Sdl3WindowTest, TitleRoundTrips)
{
    window_->SetTitle("renamed");
    EXPECT_EQ(window_->GetTitle(), "renamed");
}

TEST_F(Sdl3WindowTest, SizeChangeIsObservableAfterSync)
{
    window_->SetSize(400, 300);
    window_->Sync();
    const WindowBounds bounds = window_->GetClientBounds();
    EXPECT_EQ(bounds.width, 400);
    EXPECT_EQ(bounds.height, 300);
}

TEST_F(Sdl3WindowTest, PixelSizeIsReportedIndependentlyOfLogicalSize)
{
    // They are equal at 1:1 and differ under high DPI. What matters is that both are answered
    // and positive -- a renderer sizing a swapchain from a zero would produce nothing.
    window_->Sync();
    const WindowSize pixels = window_->GetPixelSize();
    EXPECT_GT(pixels.width, 0);
    EXPECT_GT(pixels.height, 0);
}

TEST_F(Sdl3WindowTest, DisplayScaleIsNeverZero)
{
    // SDL reports 0.0f on failure, and a zero scale divides to infinity in any layout maths, so
    // the wrapper substitutes 1:1 for an unknown scale.
    EXPECT_GT(window_->GetDisplayScale(), 0.0f);
}

TEST_F(Sdl3WindowTest, FullscreenModeStartsWindowed)
{
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::Windowed);
}

TEST_F(Sdl3WindowTest, VisibilityAndStateCallsAreAccepted)
{
    // A hidden window under the dummy driver will not report focus or minimisation changes, so
    // the assertion is that these calls are safe and do not throw -- their visible effect is a
    // display-server behaviour this environment cannot observe.
    EXPECT_NO_THROW(window_->Show());
    EXPECT_NO_THROW(window_->Hide());
    EXPECT_NO_THROW(window_->Minimize());
    EXPECT_NO_THROW(window_->Restore());
    EXPECT_NO_THROW(window_->Maximize());
    EXPECT_NO_THROW(window_->Restore());
    EXPECT_NO_THROW(window_->SetResizable(false));
    EXPECT_NO_THROW(window_->SetBorderless(true));
    EXPECT_NO_THROW(window_->Sync());
}

// --- native handle (PLAT-32) -----------------------------------------------------------------------

TEST_F(Sdl3WindowTest, NativeHandleIsSelfConsistentForItsWindowSystem)
{
    // The core invariant: whatever system is reported, the fields that system requires are the
    // ones populated. This is what a renderer's typed accessor relies on.
    const NativeWindowHandle handle = window_->GetNativeHandle();

    switch (handle.system)
    {
        case NativeWindowSystem::Win32:
        {
            Win32NativeWindow out;
            EXPECT_TRUE(TryGetWin32(handle, out));
            EXPECT_NE(out.hwnd, nullptr);
            break;
        }
        case NativeWindowSystem::X11:
        {
            X11NativeWindow out;
            EXPECT_TRUE(TryGetX11(handle, out));
            EXPECT_NE(out.display, nullptr);
            EXPECT_NE(out.window, 0u);
            // The XID must arrive through the integer field, never the pointer one.
            EXPECT_EQ(handle.window, nullptr);
            break;
        }
        case NativeWindowSystem::Wayland:
        {
            WaylandNativeWindow out;
            EXPECT_TRUE(TryGetWayland(handle, out));
            EXPECT_NE(out.display, nullptr);
            EXPECT_NE(out.surface, nullptr);
            break;
        }
        case NativeWindowSystem::Cocoa:
        {
            CocoaNativeWindow out;
            EXPECT_TRUE(TryGetCocoa(handle, out));
            break;
        }
        case NativeWindowSystem::Android:
        {
            AndroidNativeWindow out;
            EXPECT_TRUE(TryGetAndroid(handle, out));
            break;
        }
        case NativeWindowSystem::Web:
        case NativeWindowSystem::Headless:
        case NativeWindowSystem::Terminal:
        case NativeWindowSystem::Unknown:
            // The dummy/offscreen drivers land here. Reporting no native window is the correct
            // answer, and it is what makes a GPU renderer refuse instead of dereferencing a null
            // it was told was valid.
            EXPECT_FALSE(HasNativeWindow(handle));
            break;
    }
}

TEST_F(Sdl3WindowTest, NativeHandleNeverClaimsAWindowItDoesNotHave)
{
    // HasNativeWindow() is what a renderer checks before initialising. It must agree with the
    // typed accessors: claiming a usable window while every accessor refuses would send a
    // renderer down a path that then fails inside its own graphics API.
    const NativeWindowHandle handle = window_->GetNativeHandle();

    Win32NativeWindow win32;
    X11NativeWindow x11;
    WaylandNativeWindow wayland;
    CocoaNativeWindow cocoa;
    AndroidNativeWindow android;
    const bool anyAccessorSucceeds = TryGetWin32(handle, win32) || TryGetX11(handle, x11) ||
                                     TryGetWayland(handle, wayland) || TryGetCocoa(handle, cocoa) ||
                                     TryGetAndroid(handle, android);

    EXPECT_EQ(HasNativeWindow(handle), anyAccessorSucceeds) << Describe(handle);
}

TEST_F(Sdl3WindowTest, NativeHandleIsStableAcrossCalls)
{
    // A renderer stores the handle once at initialisation; it must not change underneath it.
    const NativeWindowHandle first = window_->GetNativeHandle();
    const NativeWindowHandle second = window_->GetNativeHandle();
    EXPECT_EQ(first.system, second.system);
    EXPECT_EQ(first.window, second.window);
    EXPECT_EQ(first.display, second.display);
    EXPECT_EQ(first.surface, second.surface);
    EXPECT_EQ(first.windowId, second.windowId);
}

TEST_F(Sdl3WindowTest, DescribeReportsTheSystemWithoutLeakingAddresses)
{
    const std::string described = Describe(window_->GetNativeHandle());
    EXPECT_FALSE(described.empty());
    EXPECT_EQ(described.find("0x"), std::string::npos) << "handle descriptions must not print addresses";
}

} // namespace
