// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Content::Pipeline
{
    /**
     * @brief The S3TC block formats an XNA 4.0 content build can produce.
     *
     * These are the three formats `SurfaceFormat::Dxt1`, `Dxt3` and `Dxt5` name, and the three
     * `CnbTextureFormat::Bc1`, `Bc2` and `Bc3` store. Every one packs a 4x4 texel block into a
     * fixed number of bytes, so a compressed texture's size depends only on its dimensions.
     */
    enum class BlockCompressionFormat
    {
        /** @brief BC1 / DXT1: 8 bytes per block, colour only plus an optional 1-bit alpha. */
        Bc1,

        /** @brief BC2 / DXT3: 16 bytes per block, 4-bit explicit alpha plus BC1 colour. */
        Bc2,

        /** @brief BC3 / DXT5: 16 bytes per block, interpolated alpha plus BC1 colour. */
        Bc3
    };

    /** @brief Encoder effort and policy, all of it deterministic. */
    struct BlockCompressionOptions
    {
        /**
         * @brief Least-squares endpoint refinement rounds per block.
         *
         * Zero encodes straight from the block's colour bounding box. Each further round
         * re-fits the endpoints to the indices the previous round chose. The encoder keeps the
         * best result it has seen rather than the last one, so raising this can never make a
         * block worse. Beyond about four rounds the fit has converged for almost every block.
         */
        std::uint32_t refinementRounds = 4u;

        /**
         * @brief BC1 transparency threshold.
         *
         * BC1 stores one bit of alpha, so a texel is either fully opaque or fully transparent.
         * A texel whose source alpha is below this value becomes transparent. Ignored by BC2 and
         * BC3, which store real alpha.
         */
        std::uint8_t alphaCutoff = 128u;
    };

    /**
     * @brief Bytes one 4x4 block occupies in a given format.
     *
     * @param format The block format.
     * @return 8 for BC1, 16 for BC2 and BC3.
     */
    [[nodiscard]] std::size_t BlockCompressedBlockByteCount(BlockCompressionFormat format);

    /**
     * @brief Bytes a whole image occupies once block compressed.
     *
     * Dimensions are rounded up to whole blocks, which is why a 5x5 image costs the same as an
     * 8x8 one.
     *
     * @param format The block format.
     * @param width Image width in texels.
     * @param height Image height in texels.
     * @return The exact encoded byte count.
     */
    [[nodiscard]] std::size_t BlockCompressedByteCount(BlockCompressionFormat format,
                                                       std::uint32_t width, std::uint32_t height);

    /**
     * @brief Renders a block format's name for diagnostics.
     *
     * @param format The block format.
     * @return The XNA-facing name: `Dxt1`, `Dxt3` or `Dxt5`.
     */
    [[nodiscard]] std::string BlockCompressionFormatName(BlockCompressionFormat format);

    /**
     * @brief Compresses an 8-bit RGBA image into 4x4 blocks.
     *
     * The encoding is entirely integer arithmetic and depends on nothing but its inputs, so the
     * same image always produces the same bytes on every machine and in every build.
     *
     * Texels outside the image in a partial edge block repeat the nearest edge texel, so the
     * padding never pulls the block's endpoints toward a colour the image does not contain.
     *
     * @param format Block format to produce.
     * @param rgba Exactly `width * height * 4` bytes in R, G, B, A order.
     * @param width Image width in texels, at least 1.
     * @param height Image height in texels, at least 1.
     * @param options Encoder effort and policy.
     * @return The encoded blocks, row-major by block.
     * @throws std::invalid_argument for a zero dimension or a pixel buffer of the wrong size.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeBlockCompressedImage(
        BlockCompressionFormat format, std::span<const std::uint8_t> rgba, std::uint32_t width,
        std::uint32_t height, const BlockCompressionOptions& options = {});

    /**
     * @brief Whether any texel is not fully opaque.
     *
     * @param rgba Exactly `width * height * 4` bytes in R, G, B, A order.
     * @param width Image width in texels.
     * @param height Image height in texels.
     * @return True when at least one alpha byte is below 255.
     */
    [[nodiscard]] bool ImageHasTransparency(std::span<const std::uint8_t> rgba,
                                            std::uint32_t width, std::uint32_t height);

    /**
     * @brief Whether any texel's alpha is neither fully opaque nor fully transparent.
     *
     * This is the question that decides between BC1 and BC3 for an automatic format choice: BC1
     * can carry a cutout mask exactly, but nothing in between.
     *
     * @param rgba Exactly `width * height * 4` bytes in R, G, B, A order.
     * @param width Image width in texels.
     * @param height Image height in texels.
     * @return True when at least one alpha byte is in `[1, 254]`.
     */
    [[nodiscard]] bool ImageHasPartialTransparency(std::span<const std::uint8_t> rgba,
                                                   std::uint32_t width, std::uint32_t height);
}
