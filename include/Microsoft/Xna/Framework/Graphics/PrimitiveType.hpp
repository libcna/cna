#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Primitive topology accepted by `GraphicsDevice::DrawUserPrimitives`
     *        and similar 3D draw entry points.
     *
     * Mirrors `Microsoft.Xna.Framework.Graphics.PrimitiveType` from XNA 4.0.
     *
     * @note Status: PARTIAL. Only `TriangleList`, `TriangleStrip`, `LineList`
     *       and `LineStrip` are honored by the EasyGL backend; other backends
     *       reject 3D draws entirely.
     */
    enum class PrimitiveType
    {
        TriangleList = 0,
        TriangleStrip = 1,
        LineList = 2,
        LineStrip = 3
    };
}
