// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::Software::SoftwareTextureFormat
{
    /** @brief Returns whether a raw Software texture plane contains DXT blocks. */
    [[nodiscard]] bool IsDxt(int surfaceFormat) noexcept;

    /** @brief Returns the exact declared byte width of one uncompressed texel. */
    [[nodiscard]] int BytesPerTexel(int surfaceFormat);

    /** @brief Returns the exact declared byte count of one complete texture level. */
    [[nodiscard]] std::size_t RawByteCount(int surfaceFormat, int width, int height);

    /**
     * @brief Decodes exact declared-format bytes into display and shader-visible RGBA planes.
     *
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @param source Exact declared-format bytes.
     * @param sourceBytes Number of available source bytes.
     * @param sourceStride Uncompressed row pitch in bytes; ignored for block compression.
     * @param width Level width in texels.
     * @param height Level height in texels.
     * @param destination Receives an RGBA8 presentation plane.
     * @param samples Receives canonical float RGBA values without signed/HDR narrowing.
     */
    void DecodePixels(int surfaceFormat, const std::uint8_t* source, std::size_t sourceBytes,
                      int sourceStride, int width, int height,
                      std::vector<std::uint8_t>& destination,
                      std::vector<float>& samples);
}
