// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Target colour depth for DepthEffect's colour-quantization shader.
     *
     * Each mode quantizes the rendered colour to a fixed number of levels per
     * channel, emulating retro/limited-palette display hardware.
     */
    enum class DepthEffectMode
    {
        /** @brief 16-bit colour: RGB565 (5 red / 6 green / 5 blue bits, 65536 colours). */
        Color16Bit,
        /** @brief 8-bit colour: RGB332 (3 red / 3 green / 2 blue bits, 256 colours). */
        Color8Bit,
        /** @brief Black & white, 4-bit greyscale (16 luminance levels). */
        Grayscale4Bit,
        /** @brief Black & white, 2-bit greyscale (4 luminance levels). */
        Grayscale2Bit,
        /** @brief Black & white, 1-bit (2 luminance levels — pure black/white). */
        Grayscale1Bit,
        /**
         * @brief 216-colour "web-safe" palette (6 levels per channel: 0,51,102,153,204,255),
         * nearest-colour matched — a real fixed historical palette, unlike Color8Bit's
         * independent per-channel rounding.
         */
        Palette256,
        /** @brief Classic 16-colour EGA/CGA palette, nearest-colour matched. */
        Palette16,
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
