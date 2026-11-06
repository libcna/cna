// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/XactVariation.hpp"

namespace CNA::Internal::Audio
{
    std::size_t PickXactCandidate(
        const std::vector<XsbTrackVariationEntry>& entries, std::size_t exclude,
        const XactVariationRandom& random)
    {
        uint32_t total = 0;
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (i != exclude) total += entries[i].weight;
        // Preserve CNA's existing degenerate-weight fallback, not a native claim.
        if (total == 0)
        {
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (i != exclude) return i;
            return 0;
        }
        const auto next = random.bounded(total);
        for (std::size_t i = entries.size(); i > 0; --i)
        {
            const auto index = i - 1;
            if (index == exclude) continue;
            if (next >= total - entries[index].weight) return index;
            total -= entries[index].weight;
        }
        return 0;
    }

    void SelectXactVariationCandidate(const XsbWaveRef& wave,
        XactVariationState& state, const XactVariationRandom& random)
    {
        // FACT_internal.c:734-771 initializes once, :199-247 advances each time.
        // Preserve TQ-022's discrete [0,total) boundary convention (>=).
        const auto& entries = wave.trackVariationEntries;
        if (!entries.empty())
        {
            if (!state.started)
                state.candidate = wave.trackVariationType == XsbTrackVariationType::Ordered
                    ? entries.size() - 1 : PickXactCandidate(entries, entries.size(), random);
            switch (wave.trackVariationType)
            {
                case XsbTrackVariationType::Ordered:
                case XsbTrackVariationType::OrderedFromRandom:
                    state.candidate = (state.candidate + 1) % entries.size();
                    break;
                case XsbTrackVariationType::Random:
                    state.candidate = PickXactCandidate(entries, entries.size(), random);
                    break;
                case XsbTrackVariationType::RandomNoRepeats:
                case XsbTrackVariationType::Shuffle:
                    state.candidate = PickXactCandidate(entries, state.candidate, random);
                    break;
            }
        }
    }

    void ApplyXactVariationEffects(const XsbWaveRef& wave,
        XactVariationState& state, const XactVariationRandom& random)
    {
        const auto flags = wave.effectVariationFlags;
        if (flags & 0x1000)
        {
            // FACT_internal.c:309-334: cast the span product before adding min.
            const float draw = static_cast<int16_t>(random.unit() *
                (wave.effectMaxPitch - wave.effectMinPitch)) + wave.effectMinPitch;
            if (!state.started || (flags & 0x0100))
                state.pitchCentsDelta = state.started && (flags & 0x0004)
                    ? state.pitchCentsDelta + draw : draw;
        }
        if (flags & 0x2000)
        {
            // FACT_internal.c:341-374: accumulate in centibels before conversion.
            const float draw = random.unit() * (wave.effectMaxVolume - wave.effectMinVolume)
                + wave.effectMinVolume;
            if (!state.started || (flags & 0x0200))
                state.volumeCentibels = state.started && (flags & 0x0001)
                    ? state.volumeCentibels + draw : draw;
        }
        if (flags & 0xc000)
        {
            // FACT_internal.c:381-424: Q draw precedes frequency. ADD is TODO
            // there (:400-414); retain that replacement fallback, not invented math.
            const float oneOverQ = 1.0f / (random.unit() *
                (wave.effectMaxQFactor - wave.effectMinQFactor) + wave.effectMinQFactor);
            const float frequency = random.unit() *
                (wave.effectMaxFrequency - wave.effectMinFrequency) + wave.effectMinFrequency;
            if (!state.started || (flags & 0x0c00))
            {
                state.hasFilterOverride = true;
                state.filterOneOverQ = oneOverQ;
                state.filterFrequencyHz = frequency;
            }
        }
        // No NEW bit: retain the prior axis while consuming the same draws.
        // FACT_internal.c:1284-1289,1538-1543 copies upcomingWave to activeWave
        // then clears only upcomingWave.wave; its numeric base axes survive.
        state.started = true;
    }

    void AdvanceXactVariation(const XsbWaveRef& wave,
        XactVariationState& state, const XactVariationRandom& random)
    {
        SelectXactVariationCandidate(wave, state, random);
        ApplyXactVariationEffects(wave, state, random);
    }
}
