// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cna_graphics_extensions
 *  @{
 */

    /** @brief RGB sub-pixel mask pattern applied by CRTEffect. */
    enum class CRTMaskType
    {
        /** @brief No sub-pixel mask — scanlines/curvature/vignette only. */
        None,
        /** @brief Vertical RGB stripe pattern (Trinitron-style aperture grille). */
        ApertureGrille,
        /** @brief Row-offset RGB dot pattern (classic shadow-mask CRT). */
        ShadowMask,
    };

/** @} */ // end of cna_graphics_extensions

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
