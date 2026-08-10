// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines the faces in a cube map for the TextureCube class. */
    enum class CubeMapFace
    {
        /** @brief Positive X face in the cube map. */
        PositiveX,
        /** @brief Negative X face in the cube map. */
        NegativeX,
        /** @brief Positive Y face in the cube map. */
        PositiveY,
        /** @brief Negative Y face in the cube map. */
        NegativeY,
        /** @brief Positive Z face in the cube map. */
        PositiveZ,
        /** @brief Negative Z face in the cube map. */
        NegativeZ,
    };
}
