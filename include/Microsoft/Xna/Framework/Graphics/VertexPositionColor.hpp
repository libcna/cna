// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Vertex with `Position` and `Color` channels.
     *
     * Layout-compatible with the XNA 4.0 vertex of the same name. The struct
     * is plain old data (POD-ish) so it can be uploaded directly to a GPU
     * vertex buffer.
     */
    struct VertexPositionColor
    {
        /** @brief Position in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Per-vertex color. */
        Microsoft::Xna::Framework::Color Color;

        /** @brief Constructs a default VertexPositionColor with position (0,0,0) and white color. */
        VertexPositionColor()
            : Position(0, 0, 0), Color(255, 255, 255, 255)
        {
        }

        /**
         * @brief Constructs a VertexPositionColor with the given position and color.
         * @param position The vertex position in object space.
         * @param color    The vertex color.
         */
        VertexPositionColor(const Microsoft::Xna::Framework::Vector3& position,
                            const Microsoft::Xna::Framework::Color& color)
            : Position(position), Color(color)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionColor.
         */
        [[nodiscard]] static const ::Microsoft::Xna::Framework::Graphics::VertexDeclaration& getVertexDeclarationStatic()
        {
            using ::Microsoft::Xna::Framework::Graphics::VertexElement;
            using ::Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            using ::Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            static const ::Microsoft::Xna::Framework::Graphics::VertexDeclaration decl(
                static_cast<int>(sizeof(VertexPositionColor)),
                {
                    VertexElement(0,
                                  VertexElementFormat::Vector3,
                                  VertexElementUsage::Position,
                                  0),
                    VertexElement(static_cast<int>(sizeof(::Microsoft::Xna::Framework::Vector3)),
                                  VertexElementFormat::Color,
                                  VertexElementUsage::Color,
                                  0),
                });
            return decl;
        }
    };
}
