// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"

#include <cstddef>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor exposing AudioEngine's active-cue registry size (see the friend
    // declaration in AudioEngine.hpp) without needing XactEngineImpl's full definition, which is
    // private to AudioEngine.cpp.
    struct AudioEngineTestAccess
    {
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
