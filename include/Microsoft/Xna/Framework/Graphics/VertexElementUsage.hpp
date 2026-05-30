#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief XNA 4.0 `VertexElementUsage`.
     *
     * Mirrors the semantic channel slots from
     * `Microsoft.Xna.Framework.Graphics.VertexElementUsage`.
     *
     * @note Status: STUB. The CNA backend interprets only `Position` and
     *       `Color` for the built-in `VertexPositionColor` layout.
     */
    enum class VertexElementUsage
    {
        Position = 0,
        Color = 1,
        TextureCoordinate = 2,
        Normal = 3,
        Binormal = 4,
        Tangent = 5,
        BlendIndices = 6,
        BlendWeight = 7,
        Depth = 8,
        Fog = 9,
        PointSize = 10,
        Sample = 11,
        TessellateFactor = 12,
    };
}
