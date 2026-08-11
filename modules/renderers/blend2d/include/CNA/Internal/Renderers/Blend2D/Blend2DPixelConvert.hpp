#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

namespace CNA::Internal::Renderers::Blend2D
{
    // BL_FORMAT_PRGB32 is documented (blend2d/core/format.h) as the Cairo CAIRO_FORMAT_ARGB32 /
    // Qt QImage::Format_ARGB32_Premultiplied equivalent: a packed 0xAARRGGBB 32-bit quantity
    // stored in NATIVE byte order, exactly like those two formats. Its in-memory BYTE layout is
    // therefore host-endian-dependent -- BGRA on little-endian (the byte layout every conversion
    // function below assumes), ARGB on big-endian. CNA has no big-endian target anywhere in this
    // codebase (no CNA source references __BYTE_ORDER__/std::endian for a big-endian path), so
    // rather than leave that assumption undocumented, fail the build with a truthful diagnostic on
    // a host where it would silently produce channel-swapped pixels instead.
    static_assert(std::endian::native == std::endian::little,
                 "CNA's BLEND2D renderer assumes BL_FORMAT_PRGB32 is stored as native-endian "
                 "0xAARRGGBB, i.e. BGRA byte order on little-endian hosts (Blend2DPixelConvert.hpp). "
                 "This has not been implemented or verified for a big-endian host.");

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
    ///
    /// The `std::min(255, ...)` below is DEFENSIVE hardening against a malformed/out-of-contract
    /// PRGB32 pixel (an RGB channel greater than its own alpha byte), never a substitute for
    /// coherent premultiplied storage. Every pixel this renderer itself writes -- including
    /// BlendState::NonPremultiplied/Additive's bounded CPU correction
    /// (ApplyIndependentBlendCorrectionEXT), which saturates its logical straight-space colour to
    /// [0,1] BEFORE repremultiplying against the newly computed alpha -- provably satisfies
    /// R,G,B <= A by construction (see Blend2D_Correctness::TestPrgb32Invariant, which reads raw
    /// native bytes back and asserts this holds for real NonPremultiplied/Additive output,
    /// including antialiased/rotated edges). The clamp exists purely so a division that would
    /// otherwise silently wrap through an unchecked `uint8_t` cast instead saturates, for any
    /// future or third-party PRGB32 producer that does not uphold that invariant.
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
            dstRgba[i * 4 + 0] = static_cast<std::uint8_t>(std::min(255, (r * 255 + a / 2) / a));
            dstRgba[i * 4 + 1] = static_cast<std::uint8_t>(std::min(255, (g * 255 + a / 2) / a));
            dstRgba[i * 4 + 2] = static_cast<std::uint8_t>(std::min(255, (b * 255 + a / 2) / a));
            dstRgba[i * 4 + 3] = a;
        }
    }
} // namespace CNA::Internal::Renderers::Blend2D
