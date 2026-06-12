// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the data type of a vertex element in a vertex declaration.
    enum class VertexElementFormat
    {
        /// One single-precision float.
        Single = 0,
        /// Two single-precision floats.
        Vector2 = 1,
        /// Three single-precision floats.
        Vector3 = 2,
        /// Four single-precision floats.
        Vector4 = 3,
        /// Packed 4-byte color (BGRA). Used by VertexPositionColor.
        Color = 4,
        /// Four unsigned bytes.
        Byte4 = 5,
        /// Two signed 16-bit integers.
        Short2 = 6,
        /// Four signed 16-bit integers.
        Short4 = 7,
        /// Two signed normalized 16-bit integers.
        NormalizedShort2 = 8,
        /// Four signed normalized 16-bit integers.
        NormalizedShort4 = 9,
        /// Two 16-bit floating-point values.
        HalfVector2 = 10,
        /// Four 16-bit floating-point values.
        HalfVector4 = 11,
    };
}
