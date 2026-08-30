// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Internal::Audio
{
    /** @brief Platform-neutral result of decoding an in-memory WAVE file to signed PCM16. */
    struct DecodedWavPcm16
    {
        /** @brief Interleaved signed little-endian PCM16 sample bytes. */
        std::vector<std::uint8_t> samples;
        /** @brief Decoded sample rate in hertz. */
        std::uint32_t sampleRate = 0u;
        /** @brief Decoded channel count. */
        std::uint16_t channels = 0u;
        /** @brief Number of decoded sample frames. */
        std::uint32_t frameCount = 0u;
    };

    /**
     * @brief Decodes an in-memory WAVE file without opening an audio playback device.
     *
     * @param wav Complete RIFF/WAVE bytes.
     * @param origin Diagnostic name for the source asset.
     * @return Decoded interleaved PCM16 data and its format.
     */
    [[nodiscard]] DecodedWavPcm16 DecodeWavToPcm16(
        std::span<const std::uint8_t> wav, const std::string& origin);
}
