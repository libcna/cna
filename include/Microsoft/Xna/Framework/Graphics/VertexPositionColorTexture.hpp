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
    /** @brief Describes a vertex with position, color, and one set of texture coordinates. */
    struct VertexPositionColorTexture : public IVertexType
    {
        /** @brief Position of the vertex in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Per-vertex color. */
        Microsoft::Xna::Framework::Color Color;
        /** @brief Texture coordinates for this vertex. */
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        /** @brief Constructs a default VertexPositionColorTexture with all fields zero. */
        VertexPositionColorTexture() = default;

        /**
         * @brief Constructs a VertexPositionColorTexture with the given position, color, and texture coordinate.
         * @param position          The vertex position in object space.
         * @param color             The per-vertex color.
         * @param textureCoordinate The UV texture coordinate.
         */
        VertexPositionColorTexture(const Microsoft::Xna::Framework::Vector3& position,
                                   const Microsoft::Xna::Framework::Color& color,
                                   const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), Color(color), TextureCoordinate(textureCoordinate)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionColorTexture.
         */
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance (delegates to the static version).
         * @return Const reference to the VertexDeclaration for VertexPositionColorTexture.
         */
        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        /**
         * @brief Returns true if both vertices are equal.
         * @param o The other vertex to compare with.
         * @return True if Position, Color, and TextureCoordinate are all equal.
         */
        bool operator==(const VertexPositionColorTexture& o) const
        {
            return Position == o.Position && Color == o.Color && TextureCoordinate == o.TextureCoordinate;
        }
        /**
         * @brief Returns true if both vertices are not equal.
         * @param o The other vertex to compare with.
         * @return True if any field differs.
         */
        bool operator!=(const VertexPositionColorTexture& o) const { return !(*this == o); }
        /**
         * @brief Returns a string representation of this vertex.
         * @return A string listing Position, Color, and TextureCoordinate values.
         */
        [[nodiscard]] std::string ToString() const;
    };
}
