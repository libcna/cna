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

        // CP-17: read back the loop region cached from the originating SoundEffect at
        // construction time, to verify it was captured correctly (SDL3_mixer exposes no way to
        // read back the loop-start/max-frame play options passed to MIX_PlayTrack, so the actual
        // applied effect can't be black-box verified without decoding real mixed audio output).
        static SharpRuntime::uintcs LoopStart(const SoundEffectInstance& instance)
        {
            return instance.loopStart_;
        }
        static SharpRuntime::uintcs LoopLength(const SoundEffectInstance& instance)
        {
            return instance.loopLength_;
        }

        // CP-20: SDL3_mixer has no stereo-pan getter (MIX_SetTrackStereo has no counterpart,
        // same limitation noted for CP-3/T-4B's Apply3D coverage), so the is3D latch itself is
        // the only thing that can be verified directly -- it's the actual mechanism that keeps
        // setPanProperty() from clobbering Apply3D's own pan approximation.
        static bool Is3D(const SoundEffectInstance& instance)
        {
            return instance.is3D_;
        }

        // T-4C: wrappers for the private INTERNAL_apply* DSP methods, and a hook that drives the
        // filter's real per-sample math synchronously (the real SDL3_mixer callback only fires
        // asynchronously from the mixing thread, which would make a test flaky/need a real-time
        // wait).
        static void ApplyReverb(SoundEffectInstance& instance, float rvGain)
        {
            instance.INTERNAL_applyReverb(rvGain);
        }
        static void ApplyLowPassFilter(SoundEffectInstance& instance, float cutoff, float oneOverQ = 1.0f)
        {
            instance.INTERNAL_applyLowPassFilter(cutoff, oneOverQ);
        }
        static void ApplyHighPassFilter(SoundEffectInstance& instance, float cutoff, float oneOverQ = 1.0f)
        {
            instance.INTERNAL_applyHighPassFilter(cutoff, oneOverQ);
        }
        static void ApplyBandPassFilter(SoundEffectInstance& instance, float center, float oneOverQ = 1.0f)
        {
            instance.INTERNAL_applyBandPassFilter(center, oneOverQ);
        }
        static void ProcessFilterSamples(SoundEffectInstance& instance, float* pcm,
                                          int channels, int samples)
        {
            instance.ProcessFilterSamplesForTest(pcm, channels, samples);
        }

        // P9-XACT-011 wrappers.
        static void ApplyXactTrackFilter(SoundEffectInstance& instance, uint8_t filterType,
                                          float frequencyHz, uint8_t qfactorRaw)
        {
            instance.INTERNAL_applyXactTrackFilter(filterType, frequencyHz, qfactorRaw);
        }
        static float CalculateFilterCutoff(float frequencyHz, float sampleRate)
        {
            return SoundEffectInstance::INTERNAL_calculateFilterCutoff(frequencyHz, sampleRate);
        }
        static float CalculateFilterOneOverQ(uint8_t qfactorRaw)
        {
            return SoundEffectInstance::INTERNAL_calculateFilterOneOverQ(qfactorRaw);
        }
        static void GetFilterState(const SoundEffectInstance& instance, int& kind,
                                    float& frequency, float& oneOverQ)
        {
            instance.INTERNAL_getFilterStateForTest(kind, frequency, oneOverQ);
        }

        // P9-3D-007 wrapper.
        static float CalculatePan(float dx, float distance)
        {
            return SoundEffectInstance::INTERNAL_calculatePan(dx, distance);
        }
    };
}
