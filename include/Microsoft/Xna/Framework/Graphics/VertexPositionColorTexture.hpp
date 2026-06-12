// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes a vertex with position, color, and one set of texture coordinates.
    struct VertexPositionColorTexture : public IVertexType
    {
        /// Position of the vertex in object space.
        Microsoft::Xna::Framework::Vector3 Position;
        /// Per-vertex color.
        Microsoft::Xna::Framework::Color Color;
        /// Texture coordinates for this vertex.
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        /// Constructs a default VertexPositionColorTexture.
        VertexPositionColorTexture() = default;

        /// Constructs a VertexPositionColorTexture with the given position, color, and texture coordinate.
        VertexPositionColorTexture(const Microsoft::Xna::Framework::Vector3& position,
                                   const Microsoft::Xna::Framework::Color& color,
                                   const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), Color(color), TextureCoordinate(textureCoordinate)
        {
        }

        /// Returns the vertex declaration describing the layout of this vertex type.
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /// Returns the vertex declaration for this instance.
        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        /// Returns true if both vertices are equal.
        bool operator==(const VertexPositionColorTexture& o) const
        {
            return Position == o.Position && Color == o.Color && TextureCoordinate == o.TextureCoordinate;
        }
        /// Returns true if both vertices are not equal.
        bool operator!=(const VertexPositionColorTexture& o) const { return !(*this == o); }
        /// Returns a string representation of this vertex.
        [[nodiscard]] std::string ToString() const;
    };
}
