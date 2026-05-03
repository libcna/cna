#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    /**
     * @brief Vertex with `Position` and `Color` channels.
     *
     * Layout-compatible with the XNA 4.0 vertex of the same name. The struct
     * is plain old data (POD-ish) so it can be uploaded directly to a GPU
     * vertex buffer.
     *
     * @note Status: PARTIAL. The `VertexDeclaration`/`IVertexType` interfaces
     *       from XNA are not exposed; the EasyGL backend special-cases this
     *       layout for the early 3D pipeline.
     */
    struct VertexPositionColor {
        /** Position in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** Per-vertex color. */
        Microsoft::Xna::Framework::Color   Color;

        VertexPositionColor()
            : Position(0, 0, 0), Color(255, 255, 255, 255) {}

        VertexPositionColor(const Microsoft::Xna::Framework::Vector3& position,
                            const Microsoft::Xna::Framework::Color& color)
            : Position(position), Color(color) {}
    };
}
