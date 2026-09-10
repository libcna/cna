// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Graphics::SurfaceFormatDecoder
{
    /** @brief Selects how absent color channels expand during classic surface conversion. */
    enum class MissingColorChannels
    {
        /** @brief Texture sampling expands absent color channels to one. */
        TextureSampling,
        /** @brief XNA image saving expands absent color channels to zero. */
        ImageEncoding
    };

    /**
     * @brief Reports whether a classic surface format stores DXT blocks.
     *
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @return True for Dxt1, Dxt3, or Dxt5.
     */
    [[nodiscard]] bool IsDxt(int surfaceFormat) noexcept;

    /**
     * @brief Returns the declared byte width of one uncompressed texel.
     *
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @return Byte width of one texel.
     */
    [[nodiscard]] int BytesPerTexel(int surfaceFormat);

    /**
     * @brief Returns the exact declared byte count of a complete texture level.
     *
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @param width Level width in texels.
     * @param height Level height in texels.
     * @return Required raw byte count, including DXT block padding.
     */
    [[nodiscard]] std::size_t RawByteCount(int surfaceFormat, int width, int height);

    /**
     * @brief Decodes classic declared-format bytes into RGBA8 and canonical float planes.
     *
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @param source Exact declared-format bytes.
     * @param sourceBytes Number of available source bytes.
     * @param sourceStride Uncompressed row pitch in bytes; ignored for block compression.
     * @param width Level width in texels.
     * @param height Level height in texels.
     * @param destination Receives RGBA8 values.
     * @param samples Receives canonical float RGBA values without signed/HDR narrowing.
     * @param missingChannels Expansion rule for color channels absent from the source format.
     */
    void DecodePixels(int surfaceFormat, const std::uint8_t* source, std::size_t sourceBytes,
                      int sourceStride, int width, int height,
                      std::vector<std::uint8_t>& destination,
                      std::vector<float>& samples,
                      MissingColorChannels missingChannels =
                          MissingColorChannels::TextureSampling);
}
