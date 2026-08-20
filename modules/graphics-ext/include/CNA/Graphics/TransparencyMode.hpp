// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /** @brief How `RenderPipeline` draws the transparent half of a scene. */
    enum class TransparencyMode
    {
        /**
         * @brief No transparent phase at all — the default, and the frame is what it was before
         * transparency existed in this layer.
         */
        None,

        /**
         * @brief The application's own draws, back to front, with depth testing on and depth
         * writing off.
         *
         * The pipeline sets the state and calls the registered draw; ordering is the
         * application's, usually through `TransparentDrawList`. Correct for surfaces that do not
         * interpenetrate, and it has no answer for surfaces that do.
         */
        Sorted,

        /**
         * @brief Weighted blended order-independent transparency.
         *
         * The pipeline owns the accumulation targets and the resolve; the application's shader
         * writes through `WeightedBlendedTransparency::getAccumulationGlsl()` and no sorting is
         * needed. An approximation, and the one place sorting still wins is a small number of
         * large surfaces at very different depths.
         */
        OrderIndependent
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
