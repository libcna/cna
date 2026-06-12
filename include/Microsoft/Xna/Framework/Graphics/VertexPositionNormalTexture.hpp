// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes a vertex with position, normal vector, and one set of texture coordinates.
    struct VertexPositionNormalTexture : public IVertexType
    {
        /// Position of the vertex in object space.
        Microsoft::Xna::Framework::Vector3 Position;
        /// Surface normal at this vertex.
        Microsoft::Xna::Framework::Vector3 Normal;
        /// Texture coordinates for this vertex.
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        /// Constructs a default VertexPositionNormalTexture.
        VertexPositionNormalTexture() = default;

        /// Constructs a VertexPositionNormalTexture with the given position, normal, and texture coordinate.
        VertexPositionNormalTexture(const Microsoft::Xna::Framework::Vector3& position,
                                    const Microsoft::Xna::Framework::Vector3& normal,
                                    const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), Normal(normal), TextureCoordinate(textureCoordinate)
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
        bool operator==(const VertexPositionNormalTexture& o) const
        {
            return Position == o.Position && Normal == o.Normal && TextureCoordinate == o.TextureCoordinate;
        }
        /// Returns true if both vertices are not equal.
        bool operator!=(const VertexPositionNormalTexture& o) const { return !(*this == o); }
        /// Returns a string representation of this vertex.
        [[nodiscard]] std::string ToString() const;
    };
}
