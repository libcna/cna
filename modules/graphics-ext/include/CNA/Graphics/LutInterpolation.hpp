// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief How a colour between a lookup table's entries is worked out.
     *
     * Both agree exactly on every entry the table actually holds, so the choice only shows between
     * them -- which is where almost every pixel in a frame lands, a 32-entry table having 32 values
     * to describe 256.
     */
    enum class LutInterpolation
    {
        /**
         * @brief Blend all eight surrounding entries by their box weights.
         *
         * The cheap one, and what a hardware sampler does for free. Its weakness is on the neutral
         * axis: a grey input mixes in the six coloured corners around it, so the grey comes back
         * slightly tinted, and a tint that varies smoothly with brightness reads as a grading
         * decision rather than as a lookup artefact.
         */
        Trilinear,

        /**
         * @brief Blend the four entries of the tetrahedron the colour falls in.
         *
         * Splits the cell into six tetrahedra and interpolates within one of them. A neutral colour
         * lies exactly on the edge from the cell's black corner to its white one, so it is computed
         * from two neutral entries and stays neutral -- exactly, not approximately. The cost is
         * eight individual entry reads instead of two filtered ones.
         */
        Tetrahedral
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
