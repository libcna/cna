// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Audio
{
    /**
     * @brief Decodes block-aligned MS-ADPCM to interleaved PCM16, the exact reverse of
     *        `EncodeMsAdpcm`'s block geometry and predictor/adaptation tables.
     *
     * @param bytes Block-aligned MS-ADPCM payload (a whole number of `blockAlign`-sized blocks).
     * @param channels Channel count; one or two.
     * @param blockAlign Bytes one block occupies, as named by the format's `nBlockAlign`.
     * @param samplesPerBlock Frames one block carries, as named by the format extension's
     *        `wSamplesPerBlock`.
     * @return Interleaved signed PCM16 samples, `blockCount * samplesPerBlock` frames long.
     * @throws std::invalid_argument for a channel count, block size or byte length the format
     *         cannot describe.
     * @throws std::runtime_error when a block's stored predictor coefficient index is out of range.
     */
    [[nodiscard]] std::vector<std::int16_t> DecodeMsAdpcm(
        std::span<const std::uint8_t> bytes, std::uint16_t channels,
        std::uint16_t blockAlign, std::uint16_t samplesPerBlock);
}
