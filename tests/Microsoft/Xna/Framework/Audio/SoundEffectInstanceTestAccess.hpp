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

        // T-4C: wrappers for the private INTERNAL_apply* DSP methods, and a hook that drives the
        // filter's real per-sample math synchronously (the real SDL3_mixer callback only fires
        // asynchronously from the mixing thread, which would make a test flaky/need a real-time
        // wait).
        static void ApplyReverb(SoundEffectInstance& instance, float rvGain)
        {
            instance.INTERNAL_applyReverb(rvGain);
        }
        static void ApplyLowPassFilter(SoundEffectInstance& instance, float cutoff)
        {
            instance.INTERNAL_applyLowPassFilter(cutoff);
        }
        static void ApplyHighPassFilter(SoundEffectInstance& instance, float cutoff)
        {
            instance.INTERNAL_applyHighPassFilter(cutoff);
        }
        static void ApplyBandPassFilter(SoundEffectInstance& instance, float center)
        {
            instance.INTERNAL_applyBandPassFilter(center);
        }
        static void ProcessFilterSamples(SoundEffectInstance& instance, float* pcm,
                                          int channels, int samples)
        {
            instance.ProcessFilterSamplesForTest(pcm, channels, samples);
        }
    };
}
