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
    /** @brief Describes a vertex with position and one set of texture coordinates. */
    struct VertexPositionTexture : public IVertexType
    {
        /** @brief Position of the vertex in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Texture coordinates for this vertex. */
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        /** @brief Constructs a default VertexPositionTexture with all fields zero. */
        VertexPositionTexture() = default;

        /**
         * @brief Constructs a VertexPositionTexture with the given position and texture coordinate.
         * @param position          The vertex position in object space.
         * @param textureCoordinate The UV texture coordinate.
         */
        VertexPositionTexture(const Microsoft::Xna::Framework::Vector3& position,
                              const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), TextureCoordinate(textureCoordinate)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionTexture.
         */
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance (delegates to the static version).
         * @return Const reference to the VertexDeclaration for VertexPositionTexture.
         */
        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        /**
         * @brief Returns true if both vertices are equal.
         * @param o The other vertex to compare with.
         * @return True if Position and TextureCoordinate are equal.
         */
        bool operator==(const VertexPositionTexture& o) const
        {
            return Position == o.Position && TextureCoordinate == o.TextureCoordinate;
        }
        /**
         * @brief Returns true if both vertices are not equal.
         * @param o The other vertex to compare with.
         * @return True if any field differs.
         */
        bool operator!=(const VertexPositionTexture& o) const { return !(*this == o); }
        /**
         * @brief Returns a string representation of this vertex.
         * @return A string listing Position and TextureCoordinate values.
         */
        [[nodiscard]] std::string ToString() const;
    };
}
