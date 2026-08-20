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
     * @brief Describes a GPU-skinned vertex with position, normal, tangent, one texture
     * coordinate, and up to four bone blend weights/indices -- the layout normal-mapped (PBR)
     * rendering needs on a skinned mesh (SkinnedPbrEffect).
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Mirrors VertexPositionNormalTextureSkinned's own
     * established precedent, extended with VertexPositionNormalTangentTexture's own Tangent field
     * (plans/plan_cnj.md CNB-57/Phase 13A follow-up: PBR + skinning combination).
     */
    CNAEXT struct VertexPositionNormalTangentTextureSkinned : public IVertexType
    {
        /** @brief Position of the vertex in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Surface normal at this vertex. */
        Microsoft::Xna::Framework::Vector3 Normal;
        /**
         * @brief Surface tangent at this vertex (xyz), with the bitangent's handedness sign in
         * W (matching glTF's own TANGENT accessor convention).
         */
        Microsoft::Xna::Framework::Vector4 Tangent;
        /** @brief Texture coordinates for this vertex. */
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;
        /** @brief Blend weights for up to four bones influencing this vertex. */
        Microsoft::Xna::Framework::Vector4 BlendWeight;
        /** @brief Indices of up to four bones influencing this vertex. */
        std::array<std::uint8_t, 4> BlendIndices{0, 0, 0, 0};

        /** @brief Constructs a default VertexPositionNormalTangentTextureSkinned with all fields zero. */
        VertexPositionNormalTangentTextureSkinned() = default;

        /**
         * @brief Constructs a VertexPositionNormalTangentTextureSkinned with the given attributes.
         * @param position          The vertex position in object space.
         * @param normal            The surface normal at this vertex.
         * @param tangent           The surface tangent (xyz) and bitangent handedness sign (w).
         * @param textureCoordinate The UV texture coordinate.
         * @param blendWeight       Blend weights for up to four bones.
         * @param blendIndices      Indices of up to four bones.
         */
        VertexPositionNormalTangentTextureSkinned(const Microsoft::Xna::Framework::Vector3& position,
                                                   const Microsoft::Xna::Framework::Vector3& normal,
                                                   const Microsoft::Xna::Framework::Vector4& tangent,
                                                   const Microsoft::Xna::Framework::Vector2& textureCoordinate,
                                                   const Microsoft::Xna::Framework::Vector4& blendWeight,
                                                   const std::array<std::uint8_t, 4>& blendIndices)
            : Position(position), Normal(normal), Tangent(tangent), TextureCoordinate(textureCoordinate)
            , BlendWeight(blendWeight), BlendIndices(blendIndices)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTangentTextureSkinned.
         */
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance (delegates to the static version).
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTangentTextureSkinned.
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
        bool operator==(const VertexPositionNormalTangentTextureSkinned& o) const
        {
            return Position == o.Position && Normal == o.Normal && Tangent == o.Tangent
                && TextureCoordinate == o.TextureCoordinate
                && BlendWeight == o.BlendWeight && BlendIndices == o.BlendIndices;
        }
        /**
         * @brief Returns true if both vertices are not equal.
         * @param o The other vertex to compare with.
         * @return True if any field differs.
         */
        bool operator!=(const VertexPositionNormalTangentTextureSkinned& o) const { return !(*this == o); }
        /**
         * @brief Returns true if the given vertex equals this vertex.
         * @param other The other vertex to compare.
         * @return True if all fields are equal.
         */
        [[nodiscard]] bool Equals(const VertexPositionNormalTangentTextureSkinned& other) const { return *this == other; }
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
