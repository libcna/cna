// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
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

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
