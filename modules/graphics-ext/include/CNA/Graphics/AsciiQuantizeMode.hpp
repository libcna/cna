// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Selects how AsciiPostProcessEffect quantizes a source image into a glyph/color grid.
     *
     * Migrated unchanged from the former ASCII graphics renderer's own quantizer (plans/plan_ascii.md
     * design decision 5), now a public control on the post-process effect instead of an
     * environment-variable-only renderer setting.
     */
    enum class AsciiQuantizeMode
    {
        /** @brief Monochrome: glyph chosen by luminance rank only, fixed white foreground, no background fill. */
        BlackWhite,
        /** @brief Glyph chosen by luminance rank, plus the cell's own averaged color as foreground tint and (at quarter brightness) background fill. */
        Color
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
