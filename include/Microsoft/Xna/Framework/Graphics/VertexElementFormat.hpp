// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines the data type of a vertex element in a vertex declaration. */
    enum class VertexElementFormat
    {
        /** @brief Single 32-bit floating-point number. */
        Single = 0,
        /** @brief Two-component 32-bit floating-point number. */
        Vector2 = 1,
        /** @brief Three-component 32-bit floating-point number. */
        Vector3 = 2,
        /** @brief Four-component 32-bit floating-point number. */
        Vector4 = 3,
        /** @brief Four-component packed unsigned byte mapped to the 0–1 range (BGRA). */
        Color = 4,
        /** @brief Four-component unsigned byte. */
        Byte4 = 5,
        /** @brief Two-component signed 16-bit integer. */
        Short2 = 6,
        /** @brief Four-component signed 16-bit integer. */
        Short4 = 7,
        /** @brief Normalized two-component signed 16-bit integer. */
        NormalizedShort2 = 8,
        /** @brief Normalized four-component signed 16-bit integer. */
        NormalizedShort4 = 9,
        /** @brief Two-component 16-bit floating-point number. */
        HalfVector2 = 10,
        /** @brief Four-component 16-bit floating-point number. */
        HalfVector4 = 11,
    };
}
