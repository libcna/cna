// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "System/ArgumentException.hpp"

using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;

TEST(AvatarBodyTypeNamesEXTTest, MaleMapsToMaleContentName)
{
    EXPECT_EQ(AvatarBodyTypeToContentNameEXT(AvatarBodyType::Male), "avatar/male/avatar");
}

TEST(AvatarBodyTypeNamesEXTTest, FemaleMapsToFemaleContentName)
{
    EXPECT_EQ(AvatarBodyTypeToContentNameEXT(AvatarBodyType::Female), "avatar/female/avatar");
}

TEST(AvatarBodyTypeNamesEXTTest, BothValuesMapToDistinctNonEmptyNames)
{
    const auto male = AvatarBodyTypeToContentNameEXT(AvatarBodyType::Male);
    const auto female = AvatarBodyTypeToContentNameEXT(AvatarBodyType::Female);
    EXPECT_FALSE(male.empty());
    EXPECT_FALSE(female.empty());
    EXPECT_NE(male, female);
}

TEST(AvatarBodyTypeNamesEXTTest, UnrecognizedValueThrows)
{
    EXPECT_THROW(
        (void)AvatarBodyTypeToContentNameEXT(static_cast<AvatarBodyType>(9999)),
        System::ArgumentException);
}
