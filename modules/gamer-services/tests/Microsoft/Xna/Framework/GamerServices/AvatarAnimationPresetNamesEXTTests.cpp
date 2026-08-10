// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <utility>
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

// Task 13.5: previously only checked 4 of the 31 presets by hand; the other 27 mappings were
// only ever checked for non-emptiness (AllThirtyPresetsMapToNonEmptyName above), so a spelling
// typo in any untested mapping (e.g. "MaleSurprised" vs a hypothetical "MaleSurprized") would
// have passed every existing test. Uses a stringizing macro rather than 31 hand-typed string
// literals, so the expected name is always derived from the same enumerator token used to
// reference the value - eliminating any chance of transcribing the same typo into both the
// production mapping and this test independently.
TEST(AvatarAnimationPresetNamesEXTTest, NameMatchesEnumeratorSpelling)
{
#define AVATAR_PRESET_CASE(x) {AvatarAnimationPreset::x, #x}
    const std::pair<AvatarAnimationPreset, const char*> table[] = {
        AVATAR_PRESET_CASE(Stand0), AVATAR_PRESET_CASE(Stand1), AVATAR_PRESET_CASE(Stand2),
        AVATAR_PRESET_CASE(Stand3), AVATAR_PRESET_CASE(Stand4), AVATAR_PRESET_CASE(Stand5),
        AVATAR_PRESET_CASE(Stand6), AVATAR_PRESET_CASE(Stand7), AVATAR_PRESET_CASE(Clap),
        AVATAR_PRESET_CASE(Wave), AVATAR_PRESET_CASE(Celebrate),
        AVATAR_PRESET_CASE(FemaleIdleCheckNails), AVATAR_PRESET_CASE(FemaleIdleLookAround),
        AVATAR_PRESET_CASE(FemaleIdleShiftWeight), AVATAR_PRESET_CASE(FemaleIdleFixShoe),
        AVATAR_PRESET_CASE(FemaleAngry), AVATAR_PRESET_CASE(FemaleConfused),
        AVATAR_PRESET_CASE(FemaleLaugh), AVATAR_PRESET_CASE(FemaleCry),
        AVATAR_PRESET_CASE(FemaleShocked), AVATAR_PRESET_CASE(FemaleYawn),
        AVATAR_PRESET_CASE(MaleIdleLookAround), AVATAR_PRESET_CASE(MaleIdleStretch),
        AVATAR_PRESET_CASE(MaleIdleShiftWeight), AVATAR_PRESET_CASE(MaleIdleCheckHand),
        AVATAR_PRESET_CASE(MaleAngry), AVATAR_PRESET_CASE(MaleConfused),
        AVATAR_PRESET_CASE(MaleLaugh), AVATAR_PRESET_CASE(MaleCry),
        AVATAR_PRESET_CASE(MaleSurprised), AVATAR_PRESET_CASE(MaleYawn),
    };
#undef AVATAR_PRESET_CASE
    ASSERT_EQ(sizeof(table) / sizeof(table[0]), 31u);
    for (const auto& [preset, name] : table)
    {
        EXPECT_EQ(AvatarAnimationPresetToClipNameEXT(preset), name);
    }
}

TEST(AvatarAnimationPresetNamesEXTTest, UnrecognizedValueThrows)
{
    EXPECT_THROW(
        (void)AvatarAnimationPresetToClipNameEXT(static_cast<AvatarAnimationPreset>(9999)),
        System::ArgumentException);
}
