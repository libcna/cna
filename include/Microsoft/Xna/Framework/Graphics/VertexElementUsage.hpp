// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines the semantic usage of a vertex element within a vertex declaration. */
    enum class VertexElementUsage
    {
        /** @brief Position data. */
        Position = 0,
        /** @brief Color data. */
        Color = 1,
        /** @brief Texture coordinate data, or user-defined data. */
        TextureCoordinate = 2,
        /** @brief Normal data. */
        Normal = 3,
        /** @brief Binormal data. */
        Binormal = 4,
        /** @brief Tangent data. */
        Tangent = 5,
        /** @brief Blending indices data. */
        BlendIndices = 6,
        /** @brief Blending weight data. */
        BlendWeight = 7,
        /** @brief Depth data. */
        Depth = 8,
        /** @brief Fog data. */
        Fog = 9,
        /** @brief Point size data; usable for drawing point sprites. */
        PointSize = 10,
        /** @brief Sampler data for specifying the displacement value to look up. */
        Sample = 11,
        /** @brief Positive float tessellation factor controlling the rate of tessellation. */
        TessellateFactor = 12,
    };
}
