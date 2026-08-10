// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <string>

#include "Microsoft/Xna/Framework/TitleLocation.hpp"

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
