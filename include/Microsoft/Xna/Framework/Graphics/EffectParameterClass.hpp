// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the class of an effect parameter.
    enum class EffectParameterClass
    {
        /// The parameter is a scalar value.
        Scalar,
        /// The parameter is a vector value.
        Vector,
        /// The parameter is a matrix value.
        Matrix,
        /// The parameter is an object such as a texture or string.
        Object,
        /// The parameter is a struct.
        Struct
    };
}
