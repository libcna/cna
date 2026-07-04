// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT;

TEST(AvatarAppearanceEXTTest, DefaultSkinColorIsNavajoWhite)
{
    AvatarAppearanceEXT appearance;
    EXPECT_EQ(appearance.getSkinColorProperty(), Color::NavajoWhite);
}

TEST(AvatarAppearanceEXTTest, DefaultHairColorIsSaddleBrown)
{
    AvatarAppearanceEXT appearance;
    EXPECT_EQ(appearance.getHairColorProperty(), Color::SaddleBrown);
}

TEST(AvatarAppearanceEXTTest, SkinColorRoundTrips)
{
    AvatarAppearanceEXT appearance;
    appearance.setSkinColorProperty(Color::Black);
    EXPECT_EQ(appearance.getSkinColorProperty(), Color::Black);
}

TEST(AvatarAppearanceEXTTest, HairColorRoundTrips)
{
    AvatarAppearanceEXT appearance;
    appearance.setHairColorProperty(Color::Gold);
    EXPECT_EQ(appearance.getHairColorProperty(), Color::Gold);
}
