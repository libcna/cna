// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Internal::Graphics
{
    /** @brief A decoded portable float map: top-down rows of RGBA floats. */
    struct DecodedPfm
    {
        /** @brief Width in pixels. */
        std::uint32_t width = 0u;
        /** @brief Height in pixels. */
        std::uint32_t height = 0u;
        /** @brief Four floats per pixel, rows top to bottom; alpha is one. */
        std::vector<float> pixels;
    };

    /**
     * @brief Tells whether the bytes begin with a portable float map's signature.
     *
     * @param bytes The file's first bytes.
     * @return true for `PF` (colour) or `Pf` (greyscale).
     */
    [[nodiscard]] bool IsPfm(std::span<const std::uint8_t> bytes) noexcept;

    /**
     * @brief Decodes a portable float map.
     *
     * A PFM's rows run bottom to top when its scale is negative, which is the little-endian form
     * every writer produces; the decoded rows come back top to bottom either way.
     *
     * @param bytes The complete file.
     * @param origin Diagnostic name for the source asset.
     * @return The pixels as RGBA floats.
     * @throws std::runtime_error when the header or the payload is not a portable float map.
     */
    [[nodiscard]] DecodedPfm DecodePfm(std::span<const std::uint8_t> bytes, const std::string& origin);
}
