// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <string>

#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "CNA/Internal/TitlePath.hpp"

using Microsoft::Xna::Framework::TitleLocation;

TEST(TitleLocationTest, GetPathPropertyReturnsNonEmptyString)
{
    const std::string& path = TitleLocation::getPathProperty();
    EXPECT_FALSE(path.empty());
}

TEST(TitleLocationTest, SetPathPropertyChangesGetPathProperty)
{
    TitleLocation::setPathProperty("/tmp/cna_title_location_test");
    EXPECT_EQ(TitleLocation::getPathProperty(), "/tmp/cna_title_location_test");
}

TEST(TitleLocationTest, PathMethodMatchesGetPathProperty)
{
    TitleLocation::setPathProperty("/tmp/cna_path_method_test");
    EXPECT_EQ(TitleLocation::Path(), TitleLocation::getPathProperty());
}

TEST(TitleLocationTest, SetPathPropertyRoundTrip)
{
    const std::string expected = "/srv/game/content";
    TitleLocation::setPathProperty(expected);
    EXPECT_EQ(TitleLocation::getPathProperty(), expected);
}

TEST(TitleLocationTest, ThePathReachesTheContentManagerBelowThisModule)
{
    // CBIND-140: the content module resolves a relative RootDirectory against the title's path, and
    // can only read it from core.
    const std::string original = TitleLocation::getPathProperty();
    EXPECT_EQ(CNA::Internal::TryGetTitlePath().value_or(std::string{}), original);

    TitleLocation::setPathProperty("/tmp/cna_title_path_for_content");
    EXPECT_EQ(CNA::Internal::TryGetTitlePath().value_or(std::string{}), "/tmp/cna_title_path_for_content");

    TitleLocation::setPathProperty(original);
}
