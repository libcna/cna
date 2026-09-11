// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Internal::Graphics
{
    /** @brief The storage a DDS surface carries, as the file declares it. */
    enum class DdsSurfaceFormat
    {
        /** @brief Eight bits per channel, RGBA in that order. */
        Color,
        /** @brief BC1 blocks, kept as they are stored. */
        Dxt1,
        /** @brief BC2 blocks, kept as they are stored. */
        Dxt3,
        /** @brief BC3 blocks, kept as they are stored. */
        Dxt5,
    };

    /**
     * @brief A DDS file read into its surfaces, without decompressing anything.
     *
     * The cube decoder beside this one answers RGBA for a cube map; a content pipeline needs the
     * bytes as the author stored them, because a compressed source must be able to reach an `.xnb`
     * still compressed.
     */
    struct DdsSurfaces
    {
        /** @brief Width of level 0, in texels. */
        std::uint32_t width = 0u;
        /** @brief Height of level 0, in texels. */
        std::uint32_t height = 0u;
        /** @brief Depth of level 0 for a volume, otherwise one. */
        std::uint32_t depth = 1u;
        /** @brief Levels in each chain, at least one. */
        std::uint32_t mipCount = 1u;
        /** @brief True when the file declares a cube map. */
        bool isCube = false;
        /** @brief True when the file declares a volume. */
        bool isVolume = false;
        /** @brief The storage every surface carries. */
        DdsSurfaceFormat format = DdsSurfaceFormat::Color;
        /**
         * @brief `surfaces[face][level]`: six faces for a cube, one slice per depth for a volume,
         *        one otherwise. `Color` surfaces are converted to RGBA; compressed ones are the
         *        stored blocks.
         */
        std::vector<std::vector<std::vector<std::uint8_t>>> surfaces;
    };

    /**
     * @brief Tells whether the bytes begin with the DDS signature.
     *
     * @param bytes The file's first bytes.
     * @return true for `DDS `.
     */
    [[nodiscard]] bool IsDds(std::span<const std::uint8_t> bytes) noexcept;

    /**
     * @brief Reads a DDS file into its surfaces.
     *
     * @param bytes The complete file.
     * @param origin Diagnostic name for the source asset.
     * @return The surfaces, in the order the file stores them.
     * @throws std::runtime_error when the header is not one this reader understands -- a `DX10`
     *         extension among them, which the runtime this mirrors also refuses.
     */
    [[nodiscard]] DdsSurfaces ReadDdsSurfaces(std::span<const std::uint8_t> bytes, const std::string& origin);
}
