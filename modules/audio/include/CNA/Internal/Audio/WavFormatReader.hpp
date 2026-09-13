// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Internal::Audio
{
    /**
     * @brief A WAVE file read without converting anything: the `fmt ` fields as they are written,
     *        the `data` payload byte for byte, and the loop a `smpl` chunk names.
     *
     * The decoder in `WavDecoder.hpp` answers PCM16 whatever the source was; this one answers the
     * source. A content pipeline needs the second, because it must report and re-encode exactly
     * what the author's file holds.
     */
    struct WavFormatAndData
    {
        /** @brief The `wFormatTag` field, as written: 1 PCM, 2 MS-ADPCM, 3 float, 0xFFFE extensible. */
        std::uint16_t formatTag = 1u;
        /** @brief Channel count. */
        std::uint16_t channels = 0u;
        /** @brief Sample rate in hertz. */
        std::uint32_t sampleRate = 0u;
        /** @brief The `nAvgBytesPerSec` field, as written. */
        std::uint32_t averageBytesPerSecond = 0u;
        /** @brief The `nBlockAlign` field, as written. */
        std::uint16_t blockAlign = 0u;
        /** @brief The `wBitsPerSample` field, as written. */
        std::uint16_t bitsPerSample = 0u;
        /** @brief The bytes following the sixteen base fields, without `cbSize`. */
        std::vector<std::uint8_t> extension;
        /** @brief The `data` chunk, byte for byte. */
        std::vector<std::uint8_t> data;
        /** @brief The first frame of the loop a `smpl` chunk names, or zero. */
        std::uint32_t loopStart = 0u;
        /** @brief The frames that loop spans, or zero when the file names no loop. */
        std::uint32_t loopLength = 0u;
    };

    /**
     * @brief Reads a WAVE file's format and payload without converting either.
     *
     * @param wav Complete RIFF/WAVE bytes.
     * @param origin Diagnostic name for the source asset.
     * @return The format fields, the payload and the loop.
     * @throws std::runtime_error when the bytes are not a WAVE file with a format and a payload.
     */
    [[nodiscard]] WavFormatAndData ReadWavFormatAndData(std::span<const std::uint8_t> wav,
                                                        const std::string& origin);
}
