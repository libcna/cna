// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA
{
    /**
     * @brief The colour space a swap chain's pixels are interpreted in.
     *
     * plans/plan_modern.md `MOD-2092`. Not a rendering setting: this describes what the *display* expects
     * to receive, and getting it wrong is not subtle -- PQ-encoded pixels shown as if they were
     * sRGB are washed out and grey, and sRGB pixels shown as PQ are almost black.
     */
    enum class DisplayColorSpace
    {
        /**
         * @brief Ordinary SDR output: Rec. 709 primaries, the sRGB transfer function, white at
         * whatever the display calls full brightness. Every CNA renderer's answer today.
         */
        Srgb,

        /**
         * @brief scRGB: **linear** Rec. 709, where 1.0 is 80 nits and values above 1 and below 0
         * are legal -- the latter is how it reaches colours outside Rec. 709 without changing
         * primaries. Needs a half-float swap chain.
         */
        Scrgb,

        /**
         * @brief HDR10: Rec. 2020 primaries with the ST 2084 (PQ) transfer function, where the
         * encoded value is an **absolute** luminance in nits rather than a fraction of the
         * display's maximum. Needs a 10-bit-per-channel swap chain.
         */
        Hdr10
    };
} // namespace CNA
