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

TEST(AvatarAppearanceEXTTest, DefaultShirtColorMatchesGenerateMaterialsPlaceholder)
{
    AvatarAppearanceEXT appearance;
    EXPECT_EQ(appearance.getShirtColorProperty(), Color(0.20f, 0.35f, 0.60f));
}

TEST(AvatarAppearanceEXTTest, DefaultPantsColorMatchesGenerateMaterialsPlaceholder)
{
    AvatarAppearanceEXT appearance;
    EXPECT_EQ(appearance.getPantsColorProperty(), Color(0.15f, 0.15f, 0.20f));
}

TEST(AvatarAppearanceEXTTest, DefaultShoesColorMatchesGenerateMaterialsPlaceholder)
{
    AvatarAppearanceEXT appearance;
    EXPECT_EQ(appearance.getShoesColorProperty(), Color(0.05f, 0.05f, 0.05f));
}

TEST(AvatarAppearanceEXTTest, ShirtColorRoundTrips)
{
    AvatarAppearanceEXT appearance;
    appearance.setShirtColorProperty(Color::Red);
    EXPECT_EQ(appearance.getShirtColorProperty(), Color::Red);
}

TEST(AvatarAppearanceEXTTest, PantsColorRoundTrips)
{
    AvatarAppearanceEXT appearance;
    appearance.setPantsColorProperty(Color::Green);
    EXPECT_EQ(appearance.getPantsColorProperty(), Color::Green);
}

TEST(AvatarAppearanceEXTTest, ShoesColorRoundTrips)
{
    AvatarAppearanceEXT appearance;
    appearance.setShoesColorProperty(Color::Blue);
    EXPECT_EQ(appearance.getShoesColorProperty(), Color::Blue);
}
