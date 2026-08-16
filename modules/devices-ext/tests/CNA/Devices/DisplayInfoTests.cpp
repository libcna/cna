// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/DisplayInfo.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "CNA/Platform/WindowDescription.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using CNA::Devices::DisplayInfo;
using Microsoft::Xna::Framework::GameWindow;
using Microsoft::Xna::Framework::Rectangle;

// CnaTests is a plain console/gtest binary, not a running Game, so most tests here
// exercise the "no underlying platform window" path via a default-constructed GameWindow().
// One test creates a real hidden window through the SDL3 platform implementation to exercise the
// service path, without importing an SDL type into this module.

namespace
{
    struct VideoRelease
    {
        CNA::Platform::IPlatform* platform = nullptr;
        ~VideoRelease()
        {
            if (platform != nullptr)
            {
                platform->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
            }
        }
    };
}

TEST(DisplayInfoTests, GetContentScalePropertyReturnsZeroForWindowWithNoPlatformWindow)
{
    const GameWindow window;
    EXPECT_EQ(DisplayInfo::getContentScaleProperty(window), 0.0f);
}

TEST(DisplayInfoTests, GetSafeAreaPropertyReturnsEmptyForWindowWithNoPlatformWindow)
{
    const GameWindow window;
    EXPECT_EQ(DisplayInfo::getSafeAreaProperty(window), Rectangle::Empty);
}

TEST(DisplayInfoTests, RepeatedCallsDoNotCrash)
{
    const GameWindow window;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW({
            (void)DisplayInfo::getContentScaleProperty(window);
            (void)DisplayInfo::getSafeAreaProperty(window);
        });
    }
}

TEST(DisplayInfoTests, QueriesAgainstARealPlatformWindowReturnDocumentedValues)
{
    const std::vector<std::string> available = CNA::Platform::PlatformFactory::GetAvailable();
    if (std::find(available.begin(), available.end(), "SDL3") == available.end())
    {
        GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
    }

    std::unique_ptr<CNA::Platform::IPlatform> platform =
        CNA::Platform::PlatformFactory::Create("SDL3");
    try
    {
        platform->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "video subsystem unavailable: " << error.what();
    }
    const VideoRelease videoRelease{platform.get()};
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(*platform);

    CNA::Platform::WindowDescription description;
    description.title = "display-info-test";
    description.width = 64;
    description.height = 64;
    description.visible = false;

    std::unique_ptr<CNA::Platform::IPlatformWindow> platformWindow;
    try
    {
        platformWindow = platform->CreateWindow(description);
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "window creation unavailable: " << error.what();
    }

    const GameWindow window(platformWindow.get());

    // A real (if hidden/headless) window should report a genuine, positive content scale.
    const float contentScale = DisplayInfo::getContentScaleProperty(window);
    EXPECT_GT(contentScale, 0.0f);

    // The safe area is platform/window-manager dependent, so assert only a valid sub-rectangle.
    const Rectangle safeArea = DisplayInfo::getSafeAreaProperty(window);
    EXPECT_GT(safeArea.Width, 0);
    EXPECT_GT(safeArea.Height, 0);
    EXPECT_LE(safeArea.X + safeArea.Width, description.width);
    EXPECT_LE(safeArea.Y + safeArea.Height, description.height);
}

#endif // CNA_DEVICES
