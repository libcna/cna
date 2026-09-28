#pragma once

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"

#include <array>

// Display names for the 31 standard presets, for the avatar demos' labels. Plain example data.
namespace CNAExamplesEXT
{
    struct AvatarPresetName
    {
        Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset value;
        const char* name;
    };

    inline constexpr std::array<AvatarPresetName, 31> kAvatarPresets{{
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand0, "Stand0"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand1, "Stand1"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand2, "Stand2"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand3, "Stand3"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand4, "Stand4"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand5, "Stand5"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand6, "Stand6"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Stand7, "Stand7"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Clap, "Clap"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Wave, "Wave"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::Celebrate, "Celebrate"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleIdleCheckNails, "FemaleIdleCheckNails"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleIdleLookAround, "FemaleIdleLookAround"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleIdleShiftWeight, "FemaleIdleShiftWeight"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleIdleFixShoe, "FemaleIdleFixShoe"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleAngry, "FemaleAngry"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleConfused, "FemaleConfused"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleLaugh, "FemaleLaugh"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleCry, "FemaleCry"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleShocked, "FemaleShocked"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::FemaleYawn, "FemaleYawn"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleIdleLookAround, "MaleIdleLookAround"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleIdleStretch, "MaleIdleStretch"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleIdleShiftWeight, "MaleIdleShiftWeight"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleIdleCheckHand, "MaleIdleCheckHand"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleAngry, "MaleAngry"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleConfused, "MaleConfused"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleLaugh, "MaleLaugh"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleCry, "MaleCry"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleSurprised, "MaleSurprised"},
        {Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset::MaleYawn, "MaleYawn"},
    }};
}
