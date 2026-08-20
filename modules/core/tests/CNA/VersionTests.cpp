// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "CNA/Version.hpp"

namespace
{
    // The whole header is a compile-time constant folded away in every build; these
    // static_asserts pin the part of the contract no runtime EXPECT could check, and fail the
    // build rather than a test run if an accessor ever stops being usable in a constant
    // expression (which is what a consumer's `if constexpr` version gate depends on).
    static_assert(CNA::getVersionMajor() == CNA_VERSION_MAJOR,
                  "getVersionMajor() must be usable in a constant expression");
    static_assert(CNA::getVersionMinor() == CNA_VERSION_MINOR,
                  "getVersionMinor() must be usable in a constant expression");
    static_assert(CNA::getVersionPatch() == CNA_VERSION_PATCH,
                  "getVersionPatch() must be usable in a constant expression");
    static_assert(!CNA::getVersionString().empty(),
                  "getVersionString() must be a non-empty constant expression");
    static_assert(CNA::isPreReleaseVersion() || !CNA::isPreReleaseVersion(),
                  "isPreReleaseVersion() must be usable in a constant expression");

    // Deliberately structural rather than an equality check against a literal release: this
    // suite must keep verifying that the CMake-side version and the generated header agree
    // after a version bump, not have to be edited by every bump.

    TEST(VersionTest, ComponentsAreNonNegative)
    {
        EXPECT_GE(CNA::getVersionMajor(), 0);
        EXPECT_GE(CNA::getVersionMinor(), 0);
        EXPECT_GE(CNA::getVersionPatch(), 0);
    }

    TEST(VersionTest, StringStartsWithTheNumericComponents)
    {
        const std::string numeric = std::to_string(CNA::getVersionMajor()) + '.'
                                    + std::to_string(CNA::getVersionMinor()) + '.'
                                    + std::to_string(CNA::getVersionPatch());

        EXPECT_TRUE(CNA::getVersionString().starts_with(numeric)) << CNA::getVersionString();
    }

    TEST(VersionTest, StringAppendsThePreReleaseIdentifierExactlyWhenThereIsOne)
    {
        const std::string_view version = CNA::getVersionString();
        const std::string_view preRelease = CNA::getVersionPreRelease();

        if (preRelease.empty())
        {
            EXPECT_EQ(std::string_view::npos, version.find('-')) << version;
        }
        else
        {
            EXPECT_TRUE(version.ends_with(std::string("-") + std::string(preRelease))) << version;
        }
    }

    TEST(VersionTest, IsPreReleaseVersionMatchesThePreReleaseIdentifier)
    {
        EXPECT_EQ(!CNA::getVersionPreRelease().empty(), CNA::isPreReleaseVersion());
    }

    TEST(VersionTest, StringIsSemanticVersioningSpellingWithoutAVPrefixOrWhitespace)
    {
        const std::string_view version = CNA::getVersionString();

        ASSERT_FALSE(version.empty());
        EXPECT_NE('v', version.front()) << version;
        EXPECT_EQ(std::string_view::npos, version.find(' ')) << version;
        EXPECT_EQ(std::string_view::npos, version.find('\t')) << version;
    }

    TEST(VersionTest, MacrosAndAccessorsReportTheSameRelease)
    {
        EXPECT_EQ(CNA_VERSION_MAJOR, CNA::getVersionMajor());
        EXPECT_EQ(CNA_VERSION_MINOR, CNA::getVersionMinor());
        EXPECT_EQ(CNA_VERSION_PATCH, CNA::getVersionPatch());
        EXPECT_EQ(std::string_view{CNA_VERSION_PRERELEASE}, CNA::getVersionPreRelease());
        EXPECT_EQ(std::string_view{CNA_VERSION_STRING}, CNA::getVersionString());
    }
}
