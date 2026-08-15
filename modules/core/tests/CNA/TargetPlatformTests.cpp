#include <gtest/gtest.h>

#include <string_view>

#include "CNA/DesktopOS.hpp"
#include "CNA/TargetPlatform.hpp"

using CNA::DesktopOS;
using CNA::TargetPlatform;

namespace
{
    // Every helper in CNA/TargetPlatform.hpp is a compile-time constant folded away in release builds;
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

    TEST(TargetPlatformTest, IsMobilePlatformMatchesTheMobilePlatformValues)
    {
        const TargetPlatform platform = CNA::getCurrentPlatform();
        const bool expected = platform == TargetPlatform::Android || platform == TargetPlatform::iOS;

        EXPECT_EQ(expected, CNA::isMobilePlatform());
    }

    TEST(TargetPlatformTest, WebAndDesktopAreNotMobile)
    {
        if (CNA::getCurrentPlatform() == TargetPlatform::Web ||
            CNA::getCurrentPlatform() == TargetPlatform::Desktop)
        {
            EXPECT_FALSE(CNA::isMobilePlatform());
        }
        else
        {
            EXPECT_TRUE(CNA::isMobilePlatform());
        }
    }

    // macOS reports TargetPlatform::Desktop (it is a desktop) while iOS has its own value, so
    // isApplePlatform() is exactly "one of those two", never derivable from TargetPlatform alone.
    TEST(TargetPlatformTest, IsApplePlatformCoversBothMacOsAndIos)
    {
        if (CNA::getCurrentPlatform() == TargetPlatform::iOS)
        {
            EXPECT_TRUE(CNA::isApplePlatform());
        }

#if defined(CNA_TARGET_APPLE)
        EXPECT_TRUE(CNA::isApplePlatform());
#else
        EXPECT_FALSE(CNA::isApplePlatform());
#endif
    }

    TEST(TargetPlatformTest, ApplePlatformMacrosAreMutuallyExclusive)
    {
#if defined(CNA_TARGET_IOS) && defined(CNA_TARGET_MACOS)
        FAIL() << "CNA_TARGET_IOS and CNA_TARGET_MACOS must never both be defined";
#elif defined(CNA_TARGET_APPLE)
        // Exactly one of the two must accompany CNA_TARGET_APPLE.
#  if !defined(CNA_TARGET_IOS) && !defined(CNA_TARGET_MACOS)
        FAIL() << "CNA_TARGET_APPLE requires either CNA_TARGET_IOS or CNA_TARGET_MACOS";
#  endif
        SUCCEED();
#else
        SUCCEED();
#endif
    }

    TEST(TargetPlatformTest, PlatformNameMatchesThePlatformValue)
    {
        const std::string_view name = CNA::getCurrentPlatformName();

        switch (CNA::getCurrentPlatform())
        {
            case TargetPlatform::Web:
                EXPECT_EQ("Web", name);
                break;
            case TargetPlatform::Android:
                EXPECT_EQ("Android", name);
                break;
            case TargetPlatform::iOS:
                EXPECT_EQ("iOS", name);
                break;
            case TargetPlatform::Desktop:
                EXPECT_NE("Web", name);
                EXPECT_NE("Android", name);
                EXPECT_NE("iOS", name);
                break;
        }
    }

    TEST(TargetPlatformTest, MacOsPlatformNameIsReportedForTheMacOsDesktop)
    {
#if defined(CNA_TARGET_MACOS)
        EXPECT_EQ(std::string_view("macOS"), std::string_view(CNA::getCurrentPlatformName()));
        EXPECT_EQ(TargetPlatform::Desktop, CNA::getCurrentPlatform());
        EXPECT_EQ(DesktopOS::MacOSX, CNA::getCurrentDesktopOS());
#else
        EXPECT_NE(std::string_view("macOS"), std::string_view(CNA::getCurrentPlatformName()));
#endif
    }

    // getCurrentDesktopOS() is only defined for TargetPlatform::Desktop and throws otherwise -- which
    // is the whole reason iOS needs TargetPlatform/getCurrentPlatformName() instead of a DesktopOS
    // value for the same question.
    TEST(TargetPlatformTest, DesktopOsQueryThrowsOnNonDesktopPlatforms)
    {
        if (CNA::getCurrentPlatform() == TargetPlatform::Desktop)
        {
            EXPECT_NO_THROW((void) CNA::getCurrentDesktopOS());
        }
        else
        {
            EXPECT_ANY_THROW((void) CNA::getCurrentDesktopOS());
        }
    }
}
