// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines options for filling the primitive during rasterization. */
    enum class FillMode
    {
        /** @brief Draw solid faces for each primitive. */
        Solid,
        /** @brief Draw lines for each primitive edge (wireframe). */
        WireFrame,
    };
}
