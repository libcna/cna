// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Audio
{
    /**
     * @brief Decodes block-aligned IMA/DVI ADPCM (WAVE_FORMAT_DVI_ADPCM, 0x0011) to interleaved
     *        PCM16, using the standard IMA step/index tables.
     *
     * Each block starts with a 4-byte header per channel (an int16 initial predictor, a step-index
     * byte, and a reserved byte); mono packs one nibble stream low-nibble-first, and stereo
     * interleaves the two channels' nibbles in 4-byte (8-nibble) groups, matching the format every
     * Microsoft IMA ADPCM WAV encoder writes.
     *
     * @param bytes Block-aligned IMA-ADPCM payload (a whole number of `blockAlign`-sized blocks).
     * @param channels Channel count; one or two.
     * @param blockAlign Bytes one block occupies, as named by the format's `nBlockAlign`.
     * @param samplesPerBlock Frames one block carries.
     * @return Interleaved signed PCM16 samples, `blockCount * samplesPerBlock` frames long.
     * @throws std::invalid_argument for a channel count, block size or byte length the format
     *         cannot describe.
     */
    [[nodiscard]] std::vector<std::int16_t> DecodeImaAdpcm(
        std::span<const std::uint8_t> bytes, std::uint16_t channels,
        std::uint16_t blockAlign, std::uint16_t samplesPerBlock);
}
