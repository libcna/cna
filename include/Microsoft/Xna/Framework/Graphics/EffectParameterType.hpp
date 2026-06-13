// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Defines the data type of an effect parameter.
     */
    enum class EffectParameterType
    {
        /** @brief The parameter is a void pointer. */
        Void,
        /** @brief The parameter is a Boolean value. */
        Bool,
        /** @brief The parameter is a 32-bit integer. */
        Int32,
        /** @brief The parameter is a single-precision floating-point value. */
        Single,
        /** @brief The parameter is a string. */
        String,
        /** @brief The parameter is a texture of unspecified dimension. */
        Texture,
        /** @brief The parameter is a 1D texture. */
        Texture1D,
        /** @brief The parameter is a 2D texture. */
        Texture2D,
        /** @brief The parameter is a 3D texture. */
        Texture3D,
        /** @brief The parameter is a cube texture. */
        TextureCube
    };
}
