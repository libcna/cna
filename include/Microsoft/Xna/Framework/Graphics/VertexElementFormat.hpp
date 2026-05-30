#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief XNA 4.0 `VertexElementFormat`.
     *
     * Subset of the formats from `Microsoft.Xna.Framework.Graphics.VertexElementFormat`.
     *
     * @note Status: STUB. Only the formats actually used by the supported
     *       built-in vertex types (`VertexPositionColor`) are honored by
     *       the EasyGL backend; the remaining values are kept for API-shape
     *       compatibility.
     */
    enum class VertexElementFormat
    {
        Single = 0,
        Vector2 = 1,
        Vector3 = 2,
        Vector4 = 3,
        /** Packed 4-byte color (BGRA). Used by `VertexPositionColor`. */
        Color = 4,
        Byte4 = 5,
        Short2 = 6,
        Short4 = 7,
        NormalizedShort2 = 8,
        NormalizedShort4 = 9,
        HalfVector2 = 10,
        HalfVector4 = 11,
    };
}
