// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Describes a GPU-skinned vertex: position, normal, one texture coordinate,
     * and up to four bone blend weights/indices.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension used by the real-rendering
     * Avatar path (see AvatarRenderer::EnableRealRenderingEXT) and any other game code that
     * hand-builds GPU-skinned meshes. Real XNA has no public skinned-vertex struct.
     */
    CNAEXT struct VertexPositionNormalTextureSkinned : public IVertexType
    {
        /** @brief Position of the vertex in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Surface normal at this vertex. */
        Microsoft::Xna::Framework::Vector3 Normal;
        /** @brief Texture coordinates for this vertex. */
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;
        /** @brief Blend weights for up to four bones influencing this vertex. */
        Microsoft::Xna::Framework::Vector4 BlendWeight;
        /** @brief Indices of up to four bones influencing this vertex. */
        std::array<std::uint8_t, 4> BlendIndices{0, 0, 0, 0};

        /** @brief Constructs a default VertexPositionNormalTextureSkinned with all fields zero. */
        VertexPositionNormalTextureSkinned() = default;

        /**
         * @brief Constructs a VertexPositionNormalTextureSkinned with the given attributes.
         * @param position          The vertex position in object space.
         * @param normal            The surface normal at this vertex.
         * @param textureCoordinate The UV texture coordinate.
         * @param blendWeight       Blend weights for up to four bones.
         * @param blendIndices      Indices of up to four bones.
         */
        VertexPositionNormalTextureSkinned(const Microsoft::Xna::Framework::Vector3& position,
                                            const Microsoft::Xna::Framework::Vector3& normal,
                                            const Microsoft::Xna::Framework::Vector2& textureCoordinate,
                                            const Microsoft::Xna::Framework::Vector4& blendWeight,
                                            const std::array<std::uint8_t, 4>& blendIndices)
            : Position(position), Normal(normal), TextureCoordinate(textureCoordinate)
            , BlendWeight(blendWeight), BlendIndices(blendIndices)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTextureSkinned.
         */
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance (delegates to the static version).
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTextureSkinned.
         */
        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        /**
         * @brief Returns true if both vertices are equal.
         * @param o The other vertex to compare with.
         * @return True if all fields are equal.
         */
        bool operator==(const VertexPositionNormalTextureSkinned& o) const
        {
            return Position == o.Position && Normal == o.Normal
                && TextureCoordinate == o.TextureCoordinate
                && BlendWeight == o.BlendWeight && BlendIndices == o.BlendIndices;
        }
        /**
         * @brief Returns true if both vertices are not equal.
         * @param o The other vertex to compare with.
         * @return True if any field differs.
         */
        bool operator!=(const VertexPositionNormalTextureSkinned& o) const { return !(*this == o); }
        /**
         * @brief Returns true if the given vertex equals this vertex.
         * @param other The other vertex to compare.
         * @return True if all fields are equal.
         */
        [[nodiscard]] bool Equals(const VertexPositionNormalTextureSkinned& other) const { return *this == other; }
        /**
         * @brief Returns a hash code for this vertex.
         * @return Always returns 0 (matches the FNA-derived vertex types' hash behavior).
         */
        [[nodiscard]] std::size_t GetHashCode() const { return 0; }
        /**
         * @brief Returns a string representation of this vertex.
         * @return A string listing all field values.
         */
        [[nodiscard]] std::string ToString() const;
    };
}
