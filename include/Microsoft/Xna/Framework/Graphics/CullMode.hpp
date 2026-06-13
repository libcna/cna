// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines a culling mode for faces in the rasterization process. */
    enum class CullMode
    {
        /** @brief Do not cull faces. */
        None,
        /** @brief Cull faces with clockwise vertex order. */
        CullClockwiseFace,
        /** @brief Cull faces with counter-clockwise vertex order. */
        CullCounterClockwiseFace,
    };
}
