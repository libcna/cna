// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Which kind of punctual light a `ClusteredLightSetEXT` entry describes.
     *
     * "Punctual" is the glTF word for a light that is a point in space rather than a shape: it has
     * a position and no area, which is what makes its contribution a closed-form expression instead
     * of an integral. Area lights are the other thing, and they are not this.
     *
     * The ordinals are stable, because a light set is uploaded to the GPU with this value in it.
     */
    enum class ClusteredLightType
    {
        /** @brief Light radiating equally in every direction from a point. */
        Point = 0,

        /** @brief Light confined to a cone, with a soft edge between its inner and outer angles. */
        Spot = 1,
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
