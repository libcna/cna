// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor exposing Cue internals that multiple test files need without a real
    // WaveBank/audio device backing playback (see the friend declaration in Cue.hpp).
    struct CueTestAccess
    {
        static uint16_t CategoryIndex(const Cue& cue) { return cue.categoryIdx_; }

        static std::vector<float> ActiveInstanceVolumes(const Cue& cue)
        {
            std::vector<float> volumes;
            for (const auto& pi : cue.active_)
                if (pi.instance) volumes.push_back(pi.instance->getVolumeProperty());
            return volumes;
        }

        // Returns the Nth active SoundEffectInstance (nullptr if out of range or unresolved),
        // e.g. so a test can pair it with SoundEffectInstanceTestAccess::GetTrack() to verify
        // Apply3D's real SDL_mixer effect (T-4B).
        static SoundEffectInstance* ActiveInstance(const Cue& cue, std::size_t index)
        {
            if (index >= cue.active_.size()) return nullptr;
            return cue.active_[index].instance.get();
        }

        // P10-VAR-004: reseeds the RNG non-interactive weighted variation selection draws from,
        // so a test can deterministically predict (by independently replicating the same
        // std::mt19937 + std::uniform_int_distribution draw) which entry Cue::Play() will pick.
        static void SeedRng(unsigned int seed) { Cue::INTERNAL_seedRngForTest(seed); }

        // P11-XACT-002: direct access to the PlayWaveTrackVariation-family selection algorithm,
        // for unit-testing Ordered/OrderedFromRandom/Random/RandomNoRepeats/Shuffle behavior
        // against a synthetic entry list without a full XACT fixture.
        static std::size_t SelectTrackVariationIndex(
            const std::vector<CNA::Internal::Audio::XsbTrackVariationEntry>& entries,
            CNA::Internal::Audio::XsbTrackVariationType type)
        {
            return Cue::INTERNAL_selectTrackVariationIndexForTest(entries, type);
        }

        // P11-XACT-003: direct access to the PlayWaveEffectVariation-family randomization
        // algorithm, for unit-testing pitch/volume/filter draws against a synthetic XsbWaveRef
        // without a full XACT fixture.
        struct EffectVariationOutcome
        {
            float pitchCentsDelta = 0.0f;
            float volumeAmplitudeMultiplier = 1.0f;
            bool  hasFilterOverride = false;
            float filterFrequencyHz = 0.0f;
            float filterQFactor = 0.0f;
        };

        static EffectVariationOutcome ApplyEffectVariation(
            const CNA::Internal::Audio::XsbWaveRef& waveRef)
        {
            EffectVariationOutcome out;
            Cue::INTERNAL_applyEffectVariationForTest(
                waveRef, out.pitchCentsDelta, out.volumeAmplitudeMultiplier,
                out.hasFilterOverride, out.filterFrequencyHz, out.filterQFactor);
            return out;
        }
    };
}
