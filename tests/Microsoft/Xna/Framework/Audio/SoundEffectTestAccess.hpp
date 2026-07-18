// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#include <cstddef>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for SoundEffect's private live-instance registry (see the friend
    // declaration in SoundEffect.hpp). AUD-15-005: needed so a stress test can directly assert
    // the registry actually shrinks back to zero after many short-lived SoundEffectInstance
    // objects go out of scope, instead of only inferring it indirectly.
    struct SoundEffectTestAccess
    {
        static std::size_t GetLiveInstanceCount(const SoundEffect& effect)
        {
            return effect.GetLiveInstanceCountInternal();
        }
    };
}
