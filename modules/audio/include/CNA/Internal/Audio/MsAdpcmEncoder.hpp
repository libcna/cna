// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Audio
{
    /** @brief One MS-ADPCM encode: the block-aligned payload and the geometry it was written in. */
    struct EncodedMsAdpcm
    {
        /** @brief Block-aligned MS-ADPCM bytes. */
        std::vector<std::uint8_t> bytes;
        /** @brief Bytes one block occupies. */
        std::uint16_t blockAlign = 0u;
        /** @brief Sample frames one block carries. */
        std::uint16_t samplesPerBlock = 0u;
        /** @brief Sample frames encoded, which is the block count times the frames a block holds. */
        std::uint32_t frameCount = 0u;
    };

    /**
     * @brief The seven coefficient pairs every MS-ADPCM encoder and decoder shares.
     *
     * @return The pairs, in the order a `WAVEFORMATEX` extension lists them.
     */
    [[nodiscard]] std::span<const std::pair<std::int16_t, std::int16_t>> MsAdpcmCoefficients() noexcept;

    /**
     * @brief Encodes interleaved PCM16 as MS-ADPCM.
     *
     * The last block is padded with its final frame rather than dropped, so the encoded length is
     * always a whole number of blocks.
     *
     * @param samples Interleaved signed PCM16 frames.
     * @param channels Channel count; one or two.
     * @param samplesPerBlock Frames one block carries; 128 is what the content pipeline uses.
     * @return The encoded payload and its geometry.
     * @throws std::invalid_argument when the channel count or block size cannot be encoded.
     */
    [[nodiscard]] EncodedMsAdpcm EncodeMsAdpcm(std::span<const std::int16_t> samples, std::uint16_t channels,
                                               std::uint16_t samplesPerBlock);

    /**
     * @brief Builds the `WAVEFORMATEX` extension an MS-ADPCM format carries.
     *
     * @param samplesPerBlock Frames one block carries.
     * @return The `wSamplesPerBlock`, `wNumCoef` and coefficient table bytes.
     */
    [[nodiscard]] std::vector<std::uint8_t> MsAdpcmFormatExtension(std::uint16_t samplesPerBlock);
}
