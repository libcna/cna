// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include <cstddef>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for T-3F: whether a bank loaded via streaming/lazy per-entry reads
    // (XwbData::streaming) vs eagerly loading the whole file, how many file bytes are actually
    // resident in memory, and direct access to the private GetSoundEffect() so offset/length
    // correctness can be checked without going through a real Cue/SoundBank/audio-device pipeline.
    struct WaveBankTestAccess
    {
        static bool IsStreaming(const WaveBank& wb) { return wb.StreamingInternal(); }
        static std::size_t ResidentFileBytes(const WaveBank& wb) { return wb.ResidentFileBytesInternal(); }
        static const std::string& BankName(const WaveBank& wb) { return wb.getBankName(); }
        static const SoundEffect* GetSoundEffect(WaveBank& wb, unsigned short index)
        {
            return wb.GetSoundEffect(index);
        }
    };
}
