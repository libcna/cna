// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

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
    };
}
