// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /** @brief Tonemapping operator applied to the HDR framebuffer before display. */
    enum class TonemappingMode
    {
        /** @brief No tonemapping — HDR values are clamped to [0,1]. */
        None,
        /** @brief Reinhard tonemapping (simple, slightly desaturates highlights). */
        Reinhard,
        /** @brief Filmic tonemapping (S-curve, warm highlights). */
        Filmic,
        /** @brief ACES filmic tonemapping (physically-based, cinema standard). */
        Aces,
        /**
         * @brief Uncharted 2 filmic tonemapping (Hable's curve), normalized against a white point.
         *
         * Appended rather than inserted: the preceding values are stored in settings and compared
         * by ordinal elsewhere. Unlike Filmic, this one does not bake gamma into its curve, so the
         * pipeline's gamma step still applies to its output.
         */
        Uncharted2
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
