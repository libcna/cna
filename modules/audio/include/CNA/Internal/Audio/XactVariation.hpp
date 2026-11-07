// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Audio/XactTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace CNA::Internal::Audio
{
    // Control-thread random inputs. bounded(n) supplies an integer in [0,n).
    // Separate from value-equation RNG: an equation must not consume variation draws.
    struct XactVariationRandom
    {
        std::function<uint32_t(uint32_t)> bounded;
        std::function<float()> unit;
    };

    // Owned by one authored event, never shared by two cues/tracks/events.
    struct XactVariationState
    {
        bool started = false;
        std::size_t candidate = 0;
        float pitchCentsDelta = 0.0f;
        /** @brief Combined per-wave volume base in centibels, before RPC, event, category and fade. */
        float volumeCentibels = 0.0f;
        bool hasFilterOverride = false;
        float filterFrequencyHz = 0.0f;
        float filterOneOverQ = 0.0f;
    };

    /**
     * @brief Selects a weighted candidate, optionally excluding a prior index.
     * @param entries Authored candidate weights.
     * @param exclude Index to exclude, or entries.size() to exclude none.
     * @param random Bounded integer random source.
     * @return Selected candidate index; preserves the existing zero-weight fallback.
     */
    std::size_t PickXactCandidate(const std::vector<XsbTrackVariationEntry>& entries,
        std::size_t exclude, const XactVariationRandom& random);

    /**
     * @brief Advances the candidate index while preserving first-activation draw order.
     * @param wave Authored wave event.
     * @param state This event's retained state.
     * @param random Bounded integer random source.
     */
    void SelectXactVariationCandidate(const XsbWaveRef& wave,
        XactVariationState& state, const XactVariationRandom& random);

    /**
     * @brief Applies enabled effect draws and loop gates in authored units.
     * @param wave Authored wave event.
     * @param state This event's retained state; marks activation complete.
     * @param random Unit-float random source.
     */
    void ApplyXactVariationEffects(const XsbWaveRef& wave,
        XactVariationState& state, const XactVariationRandom& random);

    /**
     * @brief Advances candidate and effect state together for one activation.
     * @param wave Authored wave event.
     * @param state This event's retained state.
     * @param random Sources for candidate and effect draws.
     */
    void AdvanceXactVariation(const XsbWaveRef& wave,
        XactVariationState& state, const XactVariationRandom& random);
}
