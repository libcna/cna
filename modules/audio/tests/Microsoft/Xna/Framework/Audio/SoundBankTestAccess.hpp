// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"

#include <chrono>
#include <cstddef>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor exposing SoundBank's private fire-and-forget cue list (see the friend
    // declaration in SoundBank.hpp), used by multiple test files.
    struct SoundBankTestAccess
    {
        static std::size_t FireAndForgetCount(const SoundBank& bank)
        {
            return bank.fireAndForget_.size();
        }

        // Backdates the most recently added fire-and-forget entry's creation time, so sweep
        // behavior at a specific age can be tested deterministically.
        static void BackdateLastFireAndForget(SoundBank& bank, std::chrono::steady_clock::duration age)
        {
            if (!bank.fireAndForget_.empty())
            {
                bank.fireAndForget_.back().created = std::chrono::steady_clock::now() - age;
            }
        }

        // The most recently played fire-and-forget cue, so a test can verify PlayCue's 3D
        // overload actually reached Cue::Apply3D (T-4B), not just that it didn't throw.
        static Cue* LastFireAndForgetCue(const SoundBank& bank)
        {
            return bank.fireAndForget_.empty() ? nullptr : bank.fireAndForget_.back().cue.get();
        }
    };
}
