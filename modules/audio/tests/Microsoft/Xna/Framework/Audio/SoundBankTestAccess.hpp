// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <chrono>
#include <cstddef>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor exposing SoundBank's private fire-and-forget cue list (see the friend
    // declaration in SoundBank.hpp), used by multiple test files.
    struct SoundBankTestAccess
    {
        /**
         * @brief Supplies invalid DSP metadata to exercise a deferred preparation error.
         * @param bank Sound bank whose parsed sounds receive an unsupported DSP chain.
         */
        static void SetUnsupportedDspChain(SoundBank& bank)
        {
            auto* data = const_cast<CNA::Internal::Audio::XsbData*>(bank.GetXsbData());
            for (auto& sound : data->sounds) sound.dspCodes = {0, 1};
        }

        /**
         * @brief Counts caller-owned and fire-and-forget cue registrations.
         * @param bank Bank whose lifetime registry is inspected.
         * @return Number of retained cue pointers.
         */
        static std::size_t RetainedCueCount(const SoundBank& bank)
        { return bank.activeCues_.size(); }

        static std::size_t FireAndForgetCount(const SoundBank& bank)
        {
            return bank.fireAndForget_.size();
        }

        // The most recently played fire-and-forget cue, so a test can verify PlayCue's 3D
        // overload actually reached Cue::Apply3D (T-4B), not just that it didn't throw.
        static Cue* LastFireAndForgetCue(const SoundBank& bank)
        {
            return bank.fireAndForget_.empty() ? nullptr : bank.fireAndForget_.back().cue.get();
        }
    };
}
