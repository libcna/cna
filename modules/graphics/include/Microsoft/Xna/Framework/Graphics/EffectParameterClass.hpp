// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Defines the class of an effect parameter.
     */
    enum class EffectParameterClass
    {
        /** @brief The parameter is a scalar value. */
        Scalar,
        /** @brief The parameter is a vector value. */
        Vector,
        /** @brief The parameter is a matrix value. */
        Matrix,
        /** @brief The parameter is an object such as a texture or string. */
        Object,
        /** @brief The parameter is a struct. */
        Struct
    };
}
