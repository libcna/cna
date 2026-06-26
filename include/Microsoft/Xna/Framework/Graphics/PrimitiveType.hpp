// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines how vertex data is ordered into geometric primitives. */
    enum class PrimitiveType
    {
        /** @brief Renders isolated triangles; each group of three vertices defines one triangle. */
        TriangleList = 0,
        /** @brief Renders a strip of connected triangles; the back-face culling flag is flipped on even-numbered triangles. */
        TriangleStrip = 1,
        /** @brief Renders a list of isolated straight line segments. */
        LineList = 2,
        /** @brief Renders the vertices as a single polyline. */
        LineStrip = 3,
        /** @brief Treats each vertex as a single point. Vertex n defines point n. N points are drawn. */
        PointListEXT = 4
    };
}
