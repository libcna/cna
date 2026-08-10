#pragma once

#include <cstdint>

namespace CNA::Internal::Renderers::Blend2D
{
    /**
     * @brief Converts one row of straight (non-premultiplied) top-row-first RGBA8 bytes into
     * Blend2D's native BL_FORMAT_PRGB32 storage: premultiplied alpha, BGRA byte order (the
     * in-memory layout of the packed 0xAARRGGBB value on a little-endian host).
     *
     * CNA's texture/render-target transfer contract (ImageData, ITextureRenderer::GetData/
     * UpdatePixels) is always straight RGBA8, matching every other renderer in this codebase;
     * Blend2D's own raster storage is premultiplied and channel-swapped, so every transfer in or
     * out of a Blend2D-owned BLImage crosses this exact conversion -- never a raw byte copy.
     */
    inline void ConvertStraightRgbaRowToPremultipliedBgra(const std::uint8_t* srcRgba,
                                                            std::uint8_t* dstBgra,
                                                            int pixelCount)
    {
        for (int i = 0; i < pixelCount; ++i)
        {
            const std::uint8_t r = srcRgba[i * 4 + 0];
            const std::uint8_t g = srcRgba[i * 4 + 1];
            const std::uint8_t b = srcRgba[i * 4 + 2];
            const std::uint8_t a = srcRgba[i * 4 + 3];
            dstBgra[i * 4 + 0] = static_cast<std::uint8_t>((b * a + 127) / 255);
            dstBgra[i * 4 + 1] = static_cast<std::uint8_t>((g * a + 127) / 255);
            dstBgra[i * 4 + 2] = static_cast<std::uint8_t>((r * a + 127) / 255);
            dstBgra[i * 4 + 3] = a;
        }
    }

    /// Inverse of ConvertStraightRgbaRowToPremultipliedBgra: reads one row of Blend2D's native
    /// premultiplied BGRA storage and writes straight top-row-first RGBA8 bytes.
    inline void ConvertPremultipliedBgraRowToStraightRgba(const std::uint8_t* srcBgra,
                                                           std::uint8_t* dstRgba,
                                                           int pixelCount)
    {
        for (int i = 0; i < pixelCount; ++i)
        {
            const std::uint8_t b = srcBgra[i * 4 + 0];
            const std::uint8_t g = srcBgra[i * 4 + 1];
            const std::uint8_t r = srcBgra[i * 4 + 2];
            const std::uint8_t a = srcBgra[i * 4 + 3];
            if (a == 0)
            {
                dstRgba[i * 4 + 0] = 0;
                dstRgba[i * 4 + 1] = 0;
                dstRgba[i * 4 + 2] = 0;
                dstRgba[i * 4 + 3] = 0;
                continue;
            }
            dstRgba[i * 4 + 0] = static_cast<std::uint8_t>((r * 255 + a / 2) / a);
            dstRgba[i * 4 + 1] = static_cast<std::uint8_t>((g * 255 + a / 2) / a);
            dstRgba[i * 4 + 2] = static_cast<std::uint8_t>((b * 255 + a / 2) / a);
            dstRgba[i * 4 + 3] = a;
        }
    }
} // namespace CNA::Internal::Renderers::Blend2D
