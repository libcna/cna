// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the data type of an effect parameter.
    enum class EffectParameterType
    {
        /// The parameter is a void pointer.
        Void,
        /// The parameter is a Boolean value.
        Bool,
        /// The parameter is a 32-bit integer.
        Int32,
        /// The parameter is a single-precision floating-point value.
        Single,
        /// The parameter is a string.
        String,
        /// The parameter is a texture.
        Texture,
        /// The parameter is a 1D texture.
        Texture1D,
        /// The parameter is a 2D texture.
        Texture2D,
        /// The parameter is a 3D texture.
        Texture3D,
        /// The parameter is a cube texture.
        TextureCube
    };
}
