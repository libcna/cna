// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Defines a culling mode for faces in the rasterization process.
     *
     * Winding is judged on the projected triangle, as the viewer sees it on screen. XNA's default,
     * CullCounterClockwiseFace, keeps clockwise triangles -- the opposite of the counter-clockwise
     * front faces of OpenGL and glTF (see docs/gltf-conventions.md).
     */
    enum class CullMode
    {
        /** @brief Do not cull faces. */
        None,
        /** @brief Cull faces with clockwise vertex order. */
        CullClockwiseFace,
        /** @brief Cull faces with counter-clockwise vertex order. */
        CullCounterClockwiseFace,
    };
}
