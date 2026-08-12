#include <gtest/gtest.h>

#include <string_view>

#include "CNA/DesktopOS.hpp"
#include "CNA/Platform.hpp"

using CNA::DesktopOS;
using CNA::Platform;

namespace
{
    // Every helper in CNA/Platform.hpp is a compile-time constant folded away in release builds;
    // these static_asserts are the part of the contract a runtime EXPECT could never check, and
    // they fail the build rather than a test run if a helper ever stops being constexpr.
    static_assert(CNA::getCurrentPlatform() == CNA::getCurrentPlatform(),
                  "getCurrentPlatform() must be usable in a constant expression");
    static_assert(CNA::isApplePlatform() || !CNA::isApplePlatform(),
                  "isApplePlatform() must be usable in a constant expression");
    static_assert(CNA::isMobilePlatform() || !CNA::isMobilePlatform(),
                  "isMobilePlatform() must be usable in a constant expression");
    static_assert(*CNA::getCurrentPlatformName() != '\0',
                  "getCurrentPlatformName() must be a non-empty constant expression");

    TEST(PlatformTest, IsMobilePlatformMatchesTheMobilePlatformValues)
    {
        const Platform platform = CNA::getCurrentPlatform();
        const bool expected = platform == Platform::Android || platform == Platform::iOS;

        EXPECT_EQ(expected, CNA::isMobilePlatform());
    }

    TEST(PlatformTest, WebAndDesktopAreNotMobile)
    {
        if (CNA::getCurrentPlatform() == Platform::Web ||
            CNA::getCurrentPlatform() == Platform::Desktop)
        {
            EXPECT_FALSE(CNA::isMobilePlatform());
        }
        else
        {
            EXPECT_TRUE(CNA::isMobilePlatform());
        }
    }

    // macOS reports Platform::Desktop (it is a desktop) while iOS has its own value, so
    // isApplePlatform() is exactly "one of those two", never derivable from Platform alone.
    TEST(PlatformTest, IsApplePlatformCoversBothMacOsAndIos)
    {
        if (CNA::getCurrentPlatform() == Platform::iOS)
        {
            EXPECT_TRUE(CNA::isApplePlatform());
        }

#if defined(CNA_PLATFORM_APPLE)
        EXPECT_TRUE(CNA::isApplePlatform());
#else
        EXPECT_FALSE(CNA::isApplePlatform());
#endif
    }

    TEST(PlatformTest, ApplePlatformMacrosAreMutuallyExclusive)
    {
#if defined(CNA_PLATFORM_IOS) && defined(CNA_PLATFORM_MACOS)
        FAIL() << "CNA_PLATFORM_IOS and CNA_PLATFORM_MACOS must never both be defined";
#elif defined(CNA_PLATFORM_APPLE)
        // Exactly one of the two must accompany CNA_PLATFORM_APPLE.
#  if !defined(CNA_PLATFORM_IOS) && !defined(CNA_PLATFORM_MACOS)
        FAIL() << "CNA_PLATFORM_APPLE requires either CNA_PLATFORM_IOS or CNA_PLATFORM_MACOS";
#  endif
        SUCCEED();
#else
        SUCCEED();
#endif
    }

    TEST(PlatformTest, PlatformNameMatchesThePlatformValue)
    {
        const std::string_view name = CNA::getCurrentPlatformName();

        switch (CNA::getCurrentPlatform())
        {
            case Platform::Web:
                EXPECT_EQ("Web", name);
                break;
            case Platform::Android:
                EXPECT_EQ("Android", name);
                break;
            case Platform::iOS:
                EXPECT_EQ("iOS", name);
                break;
            case Platform::Desktop:
                EXPECT_NE("Web", name);
                EXPECT_NE("Android", name);
                EXPECT_NE("iOS", name);
                break;
        }
    }

    TEST(PlatformTest, MacOsPlatformNameIsReportedForTheMacOsDesktop)
    {
#if defined(CNA_PLATFORM_MACOS)
        EXPECT_EQ(std::string_view("macOS"), std::string_view(CNA::getCurrentPlatformName()));
        EXPECT_EQ(Platform::Desktop, CNA::getCurrentPlatform());
        EXPECT_EQ(DesktopOS::MacOSX, CNA::getCurrentDesktopOS());
#else
        EXPECT_NE(std::string_view("macOS"), std::string_view(CNA::getCurrentPlatformName()));
#endif
    }

    // getCurrentDesktopOS() is only defined for Platform::Desktop and throws otherwise -- which
    // is the whole reason iOS needs Platform/getCurrentPlatformName() instead of a DesktopOS
    // value for the same question.
    TEST(PlatformTest, DesktopOsQueryThrowsOnNonDesktopPlatforms)
    {
        if (CNA::getCurrentPlatform() == Platform::Desktop)
        {
            EXPECT_NO_THROW((void) CNA::getCurrentDesktopOS());
        }
        else
        {
            EXPECT_ANY_THROW((void) CNA::getCurrentDesktopOS());
        }
    }
}
