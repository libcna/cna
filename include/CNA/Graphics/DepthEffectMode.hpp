// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_NOXNA

namespace CNA::Graphics {

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
    };

} // namespace CNA::Graphics

#endif // CNA_NOXNA
