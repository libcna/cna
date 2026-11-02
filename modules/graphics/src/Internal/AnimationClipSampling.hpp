// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

namespace CNA::Internal
{
    /** @brief Requires the expected target space, nondecreasing times and finite key transforms. */
    void ValidateAnimationClip(
        const Microsoft::Xna::Framework::Graphics::AnimationClipEXT& clip,
        Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT expectedSpace);

    /** @brief Samples a nonempty, validated track using overflow-safe tick distances. */
    Microsoft::Xna::Framework::Matrix SampleAnimationTrack(
        const Microsoft::Xna::Framework::Graphics::BoneTrackEXT& track, System::TimeSpan position);
}
