// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

// plan_platform.md PLAT-SDL2-8: the mixer header, and the one accessor that needs its type, are
// behind SOUND_ENABLED. Everything else in this struct reaches CNA-side state and pure XNA math
// that exist in every audio profile, and three suites outside the mixer's own coverage
// (WaveBankTests, SoundEffectContentTypeReaderTests, MediaLibraryTestAccess) use only those.
// Including the mixer header unconditionally made all three fail to compile under
// CNA_AUDIO_PLATFORM=SDL2 and =NULL, where the engine is deliberately absent from the archive.
#ifdef SOUND_ENABLED
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for SoundEffectInstance's protected track_ handle, needed by multiple
    // test files (SoundEffectInstanceTests.cpp, CueTests.cpp, SoundBankTests.cpp) to verify
    // mixer-level effects (Play() idempotency, Apply3D's track gain -- T-4B) without a
    // public API for it (see the friend declaration in SoundEffectInstance.hpp).
    struct SoundEffectInstanceTestAccess
    {
#ifdef SOUND_ENABLED
        static MIX_Track* GetTrack(const SoundEffectInstance& instance)
        {
            return static_cast<MIX_Track*>(instance.track_);
        }
#endif

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

        // CP-20: SDL3_mixer itself still has no stereo-pan getter (MIX_SetTrackStereo has no
        // counterpart, same limitation noted for CP-3/T-4B's Apply3D coverage) -- but since
        // P11-PAN-001 (RFC-1), CNA's own DSP state does (GetPanState below), so the is3D latch is
        // no longer the ONLY thing verifiable here, just the mechanism that keeps setPanProperty()
        // from clobbering Apply3D's own pan approximation.
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

        // P10-FILTER-002/003 wrapper.
        static void ApplyRpcFilterOverride(SoundEffectInstance& instance,
                                            float rpcFrequencyHz, float rpcQFactor)
        {
            instance.INTERNAL_applyRpcFilterOverride(rpcFrequencyHz, rpcQFactor);
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

        // P12-PITCH-001 wrapper.
        static float CalculatePitchRatio(float pitch)
        {
            return SoundEffectInstance::INTERNAL_calculatePitchRatio(pitch);
        }

        // P11-PAN-001 (RFC-1) wrappers.
        static void CalculatePanCrossfeedMatrix(float pan, float& ll, float& rl, float& lr, float& rr)
        {
            SoundEffectInstance::INTERNAL_calculatePanCrossfeedMatrix(pan, ll, rl, lr, rr);
        }
        static void SetPanState(SoundEffectInstance& instance, float pan)
        {
            instance.INTERNAL_setPanStateForTest(pan);
        }
        static float GetPanState(const SoundEffectInstance& instance)
        {
            return instance.INTERNAL_getPanStateForTest();
        }

        // P9-3D-010 wrapper.
        static Microsoft::Xna::Framework::Vector3 CalculateListenerRight(
            const Microsoft::Xna::Framework::Vector3& forward,
            const Microsoft::Xna::Framework::Vector3& up)
        {
            return SoundEffectInstance::INTERNAL_calculateListenerRight(forward, up);
        }
    };
}
