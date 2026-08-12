// SPDX-License-Identifier: MS-PL
//
// PLAT-52: GraphicsDeviceManager no longer names SDL.
//
// Deliberately without the SDL video probe the rest of GraphicsDeviceManagerTests.cpp guards
// itself with: everything checked here is decided before any window exists, which is exactly why
// it can be checked in an environment with no display at all. That is also the point of the
// migration -- the orientation decision was reading SDL_GetPlatform(), a call that has nothing to
// do with graphics and pulled the SDL header into a header every game includes.

#include <gtest/gtest.h>

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <string>

using namespace Microsoft::Xna::Framework;

TEST(GraphicsDeviceManagerPlatformTest, ADefaultManagerConstructsWithNoGame)
{
    // The default constructor decides orientation support before it has a game to ask, so it
    // reads the ambient platform. If that lookup threw or returned a null system-info service,
    // simply declaring a GraphicsDeviceManager would fail.
    EXPECT_NO_THROW({ GraphicsDeviceManager manager; });
}

TEST(GraphicsDeviceManagerPlatformTest, RepeatedConstructionIsSafe)
{
    // The ambient platform is shared, so a manager that acquired something during construction
    // and failed to release it would show up here rather than at the first one.
    for (int i = 0; i < 8; ++i)
    {
        GraphicsDeviceManager manager;
        (void)manager;
    }
    SUCCEED();
}

TEST(GraphicsDeviceManagerPlatformTest, TheSystemInfoServiceIsAlwaysPresent)
{
    // The orientation decision dereferences this unconditionally. Unlike the capability-gated
    // services, system info is documented as never null -- every platform can at least name
    // itself -- and this is the call site that depends on it.
    CNA::Platform::IPlatformSystemInfo* systemInfo =
        CNA::Platform::GetCurrentPlatform().GetSystemInfo();
    ASSERT_NE(systemInfo, nullptr);
    EXPECT_FALSE(systemInfo->GetPlatformName().empty());
}

TEST(GraphicsDeviceManagerPlatformTest, DefaultBackBufferPreferencesAreTheXnaDefaults)
{
    // Orientation support is what decides whether the preferred back-buffer size is kept verbatim
    // or swapped to match a device orientation. On a desktop platform it must be kept, and these
    // are the values a swap would visibly transpose.
    GraphicsDeviceManager manager;

    const std::string platformName =
        CNA::Platform::GetCurrentPlatform().GetSystemInfo()->GetPlatformName();
    if (platformName == "iOS" || platformName == "Android")
    {
        GTEST_SKIP() << "orientation-aware platform: the back buffer is swapped by design";
    }

    EXPECT_EQ(manager.getPreferredBackBufferWidthProperty(),
              GraphicsDeviceManager::DefaultBackBufferWidth);
    EXPECT_EQ(manager.getPreferredBackBufferHeightProperty(),
              GraphicsDeviceManager::DefaultBackBufferHeight);
}

TEST(GraphicsDeviceManagerPlatformTest, PreferredBackBufferSizeRoundTrips)
{
    // A non-square size, so a transposing bug cannot pass by accident.
    GraphicsDeviceManager manager;
    manager.setPreferredBackBufferWidthProperty(1280);
    manager.setPreferredBackBufferHeightProperty(720);

    EXPECT_EQ(manager.getPreferredBackBufferWidthProperty(), 1280);
    EXPECT_EQ(manager.getPreferredBackBufferHeightProperty(), 720);
}
