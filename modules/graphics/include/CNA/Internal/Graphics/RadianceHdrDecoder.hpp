// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Internal::Graphics
{
    /** @brief A decoded Radiance image: top-down rows of RGBA floats, alpha one. */
    struct DecodedRadianceHdr
    {
        /** @brief Width in pixels. */
        std::uint32_t width = 0u;
        /** @brief Height in pixels. */
        std::uint32_t height = 0u;
        /** @brief Four floats per pixel, rows top to bottom; alpha is one. */
        std::vector<float> pixels;
    };

    /**
     * @brief Tells whether the bytes begin with a Radiance picture's signature.
     *
     * @param bytes The file's first bytes.
     * @return true for `#?RADIANCE` or `#?RGBE`.
     */
    [[nodiscard]] bool IsRadianceHdr(std::span<const std::uint8_t> bytes) noexcept;

    /**
     * @brief Decodes a Radiance RGBE picture to linear floats.
     *
     * Each stored pixel is a red, green and blue mantissa sharing one exponent, and its value is
     * `(mantissa + 0.5) / 256 * 2^(exponent - 128)` -- the convention XNA's own reader answers
     * (measured, tests/reference/xna40/graphics case textureext/probe.hdr/floats). An exponent of
     * zero is the encoding of black and answers zero rather than a denormal.
     *
     * Both scanline encodings are read: the run-length one every writer produces at a width of 8
     * or more, and the flat one a narrower image is obliged to use. XNA's reader handles only the
     * first and answers garbage for the second, which is a recorded divergence rather than a
     * behaviour to reproduce (docs/xna-content-pipeline-texture.md).
     *
     * @param bytes The complete file.
     * @param origin Diagnostic name for the source asset.
     * @return The pixels as RGBA floats.
     * @throws std::runtime_error when the header or the payload is not a Radiance picture.
     */
    [[nodiscard]] DecodedRadianceHdr DecodeRadianceHdr(std::span<const std::uint8_t> bytes,
                                                       const std::string& origin);
}
