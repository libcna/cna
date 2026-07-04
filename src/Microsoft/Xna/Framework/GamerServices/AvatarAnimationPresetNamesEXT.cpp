// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp"
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    std::string AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset preset)
    {
        switch (preset)
        {
            case AvatarAnimationPreset::Stand0: return "Stand0";
            case AvatarAnimationPreset::Stand1: return "Stand1";
            case AvatarAnimationPreset::Stand2: return "Stand2";
            case AvatarAnimationPreset::Stand3: return "Stand3";
            case AvatarAnimationPreset::Stand4: return "Stand4";
            case AvatarAnimationPreset::Stand5: return "Stand5";
            case AvatarAnimationPreset::Stand6: return "Stand6";
            case AvatarAnimationPreset::Stand7: return "Stand7";
            case AvatarAnimationPreset::Clap: return "Clap";
            case AvatarAnimationPreset::Wave: return "Wave";
            case AvatarAnimationPreset::Celebrate: return "Celebrate";
            case AvatarAnimationPreset::FemaleIdleCheckNails: return "FemaleIdleCheckNails";
            case AvatarAnimationPreset::FemaleIdleLookAround: return "FemaleIdleLookAround";
            case AvatarAnimationPreset::FemaleIdleShiftWeight: return "FemaleIdleShiftWeight";
            case AvatarAnimationPreset::FemaleIdleFixShoe: return "FemaleIdleFixShoe";
            case AvatarAnimationPreset::FemaleAngry: return "FemaleAngry";
            case AvatarAnimationPreset::FemaleConfused: return "FemaleConfused";
            case AvatarAnimationPreset::FemaleLaugh: return "FemaleLaugh";
            case AvatarAnimationPreset::FemaleCry: return "FemaleCry";
            case AvatarAnimationPreset::FemaleShocked: return "FemaleShocked";
            case AvatarAnimationPreset::FemaleYawn: return "FemaleYawn";
            case AvatarAnimationPreset::MaleIdleLookAround: return "MaleIdleLookAround";
            case AvatarAnimationPreset::MaleIdleStretch: return "MaleIdleStretch";
            case AvatarAnimationPreset::MaleIdleShiftWeight: return "MaleIdleShiftWeight";
            case AvatarAnimationPreset::MaleIdleCheckHand: return "MaleIdleCheckHand";
            case AvatarAnimationPreset::MaleAngry: return "MaleAngry";
            case AvatarAnimationPreset::MaleConfused: return "MaleConfused";
            case AvatarAnimationPreset::MaleLaugh: return "MaleLaugh";
            case AvatarAnimationPreset::MaleCry: return "MaleCry";
            case AvatarAnimationPreset::MaleSurprised: return "MaleSurprised";
            case AvatarAnimationPreset::MaleYawn: return "MaleYawn";
        }
        throw System::ArgumentException("Unrecognized AvatarAnimationPreset value.", "preset");
    }
}
