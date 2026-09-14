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
     * @brief Decodes an in-memory WAVE file to interleaved PCM16, with no SDL dependency.
     *
     * Supports PCM (8/16/24/32-bit) and IEEE float (32/64-bit) source data, including when named
     * through a WAVE_FORMAT_EXTENSIBLE format chunk, plus MS-ADPCM and IMA-ADPCM (via
     * `DecodeMsAdpcm`/`DecodeImaAdpcm`; when their format chunk omits `wSamplesPerBlock`,
     * IMA-ADPCM derives it with the standard reference formula and MS-ADPCM requires it explicit).
     * The output keeps the file's original sample rate and channel count; only the sample encoding
     * is converted. Any other encoding is refused with a clear error rather than silently
     * misdecoded.
     *
     * @param wav Complete RIFF/WAVE bytes.
     * @param origin Diagnostic name for the source asset.
     * @return Decoded interleaved PCM16 data and its format.
     * @throws std::runtime_error for a malformed file or an unsupported encoding/bit depth.
     */
    [[nodiscard]] DecodedWavPcm16 DecodeWavToPcm16(
        std::span<const std::uint8_t> wav, const std::string& origin);
}
