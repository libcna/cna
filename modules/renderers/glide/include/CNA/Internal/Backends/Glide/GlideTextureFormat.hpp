#pragma once

#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::Glide
{
    /**
     * The narrowest native Glide texture format an ARGB4444-quantized logical mip pyramid can
     * losslessly (relative to that already-quantized source) be re-packed into.
     */
    enum class GlideTextureAlphaClass
    {
        Opaque,     ///< Every texel has alpha == 0xF: safe as RGB565 (no alpha channel at all).
        Binary,     ///< Every texel has alpha == 0x0 or 0xF: safe as ARGB1555 (1-bit alpha).
        Fractional, ///< At least one texel has 0 < alpha < 0xF: must stay ARGB4444.
    };

    /**
     * Classifies one already-ARGB4444-packed mip level's alpha coverage. Short-circuits on the
     * first fractional-alpha texel found, since no coarser class can then be correct.
     */
    [[nodiscard]] inline GlideTextureAlphaClass ClassifyGlideArgb4444AlphaCoverage(
        const std::vector<std::uint16_t>& texels)
    {
        bool sawTransparent = false;
        for (const std::uint16_t texel : texels)
        {
            const std::uint16_t alpha = (texel >> 12) & 0xF;
            if (alpha != 0 && alpha != 0xF)
            {
                return GlideTextureAlphaClass::Fractional;
            }
            if (alpha == 0)
            {
                sawTransparent = true;
            }
        }
        return sawTransparent ? GlideTextureAlphaClass::Binary : GlideTextureAlphaClass::Opaque;
    }

    /** Combines two classifications (e.g. from different mip levels) into their common bound. */
    [[nodiscard]] inline GlideTextureAlphaClass CombineGlideTextureAlphaClass(
        GlideTextureAlphaClass a, GlideTextureAlphaClass b)
    {
        if (a == GlideTextureAlphaClass::Fractional || b == GlideTextureAlphaClass::Fractional)
        {
            return GlideTextureAlphaClass::Fractional;
        }
        if (a == GlideTextureAlphaClass::Binary || b == GlideTextureAlphaClass::Binary)
        {
            return GlideTextureAlphaClass::Binary;
        }
        return GlideTextureAlphaClass::Opaque;
    }

    namespace Detail
    {
        /**
         * Replicates a 4-bit channel's top `extraBits` bits into the newly-added low bits when
         * widening it, the standard technique for expanding a narrow colour channel without
         * biasing the result toward black (a plain left-shift would leave the new low bits 0,
         * so the maximum input value 0xF would never reach the new range's true maximum).
         * extraBits == 1 (4 -> 5 bits): (v << 1) | (v >> 3).
         * extraBits == 2 (4 -> 6 bits): (v << 2) | (v >> 2).
         */
        [[nodiscard]] inline std::uint16_t ExpandGlide4BitChannel(std::uint16_t value4, int extraBits)
        {
            const std::uint16_t widened = static_cast<std::uint16_t>(value4 << extraBits);
            const std::uint16_t replication = static_cast<std::uint16_t>(value4 >> (4 - extraBits));
            return static_cast<std::uint16_t>(widened | replication);
        }
    } // namespace Detail

    /**
     * Re-packs an ARGB4444 texel as RGB565 (bits 15-11 R, 10-5 G, 4-0 B; alpha discarded). Only
     * valid to use when the source has already been classified `Opaque`.
     */
    [[nodiscard]] inline std::uint16_t GlideArgb4444ToRgb565(std::uint16_t argb4444)
    {
        const std::uint16_t r5 = Detail::ExpandGlide4BitChannel((argb4444 >> 8) & 0xF, 1);
        const std::uint16_t g6 = Detail::ExpandGlide4BitChannel((argb4444 >> 4) & 0xF, 2);
        const std::uint16_t b5 = Detail::ExpandGlide4BitChannel(argb4444 & 0xF, 1);
        return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
    }

    /**
     * Re-packs an ARGB4444 texel as ARGB1555 (bit 15 A, 14-10 R, 9-5 G, 4-0 B). Only valid to use
     * when the source has already been classified `Opaque` or `Binary` (alpha is exactly 0 or 0xF).
     */
    [[nodiscard]] inline std::uint16_t GlideArgb4444ToArgb1555(std::uint16_t argb4444)
    {
        const std::uint16_t a1 = ((argb4444 >> 12) & 0xF) != 0 ? 1 : 0;
        const std::uint16_t r5 = Detail::ExpandGlide4BitChannel((argb4444 >> 8) & 0xF, 1);
        const std::uint16_t g5 = Detail::ExpandGlide4BitChannel((argb4444 >> 4) & 0xF, 1);
        const std::uint16_t b5 = Detail::ExpandGlide4BitChannel(argb4444 & 0xF, 1);
        return static_cast<std::uint16_t>((a1 << 15) | (r5 << 10) | (g5 << 5) | b5);
    }
} // namespace CNA::Internal::Backends::Glide
