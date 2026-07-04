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
    };
}
