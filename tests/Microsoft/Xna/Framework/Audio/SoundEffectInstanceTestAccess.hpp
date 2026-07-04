// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

#include <SDL3_mixer/SDL_mixer.h>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for SoundEffectInstance's protected track_ handle, needed by multiple
    // test files (SoundEffectInstanceTests.cpp, CueTests.cpp, SoundBankTests.cpp) to verify
    // SDL_mixer-level effects (Play() idempotency, Apply3D's track gain -- T-4B) without a
    // public API for it (see the friend declaration in SoundEffectInstance.hpp).
    struct SoundEffectInstanceTestAccess
    {
        static MIX_Track* GetTrack(const SoundEffectInstance& instance)
        {
            return static_cast<MIX_Track*>(instance.track_);
        }
    };
}
