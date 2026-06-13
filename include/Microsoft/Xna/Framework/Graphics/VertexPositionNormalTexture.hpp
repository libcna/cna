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
    /** @brief Describes a vertex with position, surface normal, and one set of texture coordinates. */
    struct VertexPositionNormalTexture : public IVertexType
    {
        /** @brief Position of the vertex in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Surface normal at this vertex. */
        Microsoft::Xna::Framework::Vector3 Normal;
        /** @brief Texture coordinates for this vertex. */
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        /** @brief Constructs a default VertexPositionNormalTexture with all fields zero. */
        VertexPositionNormalTexture() = default;

        /**
         * @brief Constructs a VertexPositionNormalTexture with the given position, normal, and texture coordinate.
         * @param position          The vertex position in object space.
         * @param normal            The surface normal at this vertex.
         * @param textureCoordinate The UV texture coordinate.
         */
        VertexPositionNormalTexture(const Microsoft::Xna::Framework::Vector3& position,
                                    const Microsoft::Xna::Framework::Vector3& normal,
                                    const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), Normal(normal), TextureCoordinate(textureCoordinate)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTexture.
         */
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance (delegates to the static version).
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTexture.
         */
        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        /**
         * @brief Returns true if both vertices are equal.
         * @param o The other vertex to compare with.
         * @return True if Position, Normal, and TextureCoordinate are all equal.
         */
        bool operator==(const VertexPositionNormalTexture& o) const
        {
            return Position == o.Position && Normal == o.Normal && TextureCoordinate == o.TextureCoordinate;
        }
        /**
         * @brief Returns true if both vertices are not equal.
         * @param o The other vertex to compare with.
         * @return True if any field differs.
         */
        bool operator!=(const VertexPositionNormalTexture& o) const { return !(*this == o); }
        /**
         * @brief Returns a string representation of this vertex.
         * @return A string listing Position, Normal, and TextureCoordinate values.
         */
        [[nodiscard]] std::string ToString() const;
    };
}
