// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp"
#include "System/ArgumentException.hpp"

using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPresetToClipNameEXT;

TEST(AvatarAnimationPresetNamesEXTTest, AllThirtyPresetsMapToNonEmptyName)
{
    const AvatarAnimationPreset presets[] = {
        AvatarAnimationPreset::Stand0, AvatarAnimationPreset::Stand1, AvatarAnimationPreset::Stand2,
        AvatarAnimationPreset::Stand3, AvatarAnimationPreset::Stand4, AvatarAnimationPreset::Stand5,
        AvatarAnimationPreset::Stand6, AvatarAnimationPreset::Stand7, AvatarAnimationPreset::Clap,
        AvatarAnimationPreset::Wave, AvatarAnimationPreset::Celebrate,
        AvatarAnimationPreset::FemaleIdleCheckNails, AvatarAnimationPreset::FemaleIdleLookAround,
        AvatarAnimationPreset::FemaleIdleShiftWeight, AvatarAnimationPreset::FemaleIdleFixShoe,
        AvatarAnimationPreset::FemaleAngry, AvatarAnimationPreset::FemaleConfused,
        AvatarAnimationPreset::FemaleLaugh, AvatarAnimationPreset::FemaleCry,
        AvatarAnimationPreset::FemaleShocked, AvatarAnimationPreset::FemaleYawn,
        AvatarAnimationPreset::MaleIdleLookAround, AvatarAnimationPreset::MaleIdleStretch,
        AvatarAnimationPreset::MaleIdleShiftWeight, AvatarAnimationPreset::MaleIdleCheckHand,
        AvatarAnimationPreset::MaleAngry, AvatarAnimationPreset::MaleConfused,
        AvatarAnimationPreset::MaleLaugh, AvatarAnimationPreset::MaleCry,
        AvatarAnimationPreset::MaleSurprised, AvatarAnimationPreset::MaleYawn,
    };
    EXPECT_EQ(sizeof(presets) / sizeof(presets[0]), 31u); // 8 Stand + Clap/Wave/Celebrate + 10 Female + 10 Male
    for (const auto preset : presets)
    {
        EXPECT_FALSE(AvatarAnimationPresetToClipNameEXT(preset).empty());
    }
}

TEST(AvatarAnimationPresetNamesEXTTest, NameMatchesEnumeratorSpelling)
{
    EXPECT_EQ(AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset::Wave), "Wave");
    EXPECT_EQ(AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset::Clap), "Clap");
    EXPECT_EQ(AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset::FemaleIdleCheckNails),
              "FemaleIdleCheckNails");
    EXPECT_EQ(AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset::MaleYawn), "MaleYawn");
}

TEST(AvatarAnimationPresetNamesEXTTest, UnrecognizedValueThrows)
{
    EXPECT_THROW(
        (void)AvatarAnimationPresetToClipNameEXT(static_cast<AvatarAnimationPreset>(9999)),
        System::ArgumentException);
}
