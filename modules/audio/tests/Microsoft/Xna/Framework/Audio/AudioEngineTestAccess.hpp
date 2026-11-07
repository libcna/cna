// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"

#include <cstddef>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor exposing AudioEngine's active-cue registry size (see the friend
    // declaration in AudioEngine.hpp) without needing XactEngineImpl's full definition, which is
    // private to AudioEngine.cpp.
    struct AudioEngineTestAccess
    {
        static void SetEventClock(AudioEngine& engine,
            std::function<std::chrono::steady_clock::time_point()> clock)
        { engine.eventClock_ = std::move(clock); }

        /**
         * @brief Installs per-cue random inputs before construction prepares initial waves.
         * @param engine Engine creating the cue.
         * @param random Candidate and effect random inputs copied into each new cue.
         */
        static void SetPreparationVariationRandom(AudioEngine& engine,
            CNA::Internal::Audio::XactVariationRandom random)
        { engine.preparationVariationRandom_ = std::move(random); }


        static std::size_t ActiveCueCount(const AudioEngine& engine)
        {
            return engine.ActiveCueCountForTest();
        }

        // P12-CATEGORY-001: exposes AudioEngine::GetCategoryVolume (private, called by
        // AudioCategory/Cue) so a test can verify SetVolume's parent->child hierarchy cascade by
        // reading a category's own stored volume directly, without a public AudioCategory getter
        // (matches real XNA -- Volume is a command-only property on AudioCategory.cs, no getter).
        static float GetCategoryVolume(const AudioEngine& engine, unsigned short idx)
        {
            return engine.GetCategoryVolume(idx);
        }
    };
}
