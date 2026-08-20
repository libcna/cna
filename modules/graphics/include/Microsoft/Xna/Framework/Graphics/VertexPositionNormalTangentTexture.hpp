// SPDX-License-Identifier: MS-PL
#pragma once

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
     * @brief Describes a vertex with position, normal, tangent, and one texture coordinate --
     * the layout normal-mapped (PBR) rendering needs to build a per-pixel tangent-space basis.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Real XNA predates the PBR/normal-mapping
     * content pipeline this describes. Mirrors VertexPositionNormalTextureSkinned's own
     * established precedent for a CNA-original vertex format (plans/plan_cnj.md CNB-57, Phase 13A).
     */
    CNAEXT struct VertexPositionNormalTangentTexture : public IVertexType
    {
        /** @brief Position of the vertex in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Surface normal at this vertex. */
        Microsoft::Xna::Framework::Vector3 Normal;
        /**
         * @brief Surface tangent at this vertex (xyz), with the bitangent's handedness sign in
         * W (matching glTF's own TANGENT accessor convention: Bitangent = cross(Normal, Tangent.xyz) * Tangent.W).
         */
        Microsoft::Xna::Framework::Vector4 Tangent;
        /** @brief Texture coordinates for this vertex. */
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        /** @brief Constructs a default VertexPositionNormalTangentTexture with all fields zero. */
        VertexPositionNormalTangentTexture() = default;

        /**
         * @brief Constructs a VertexPositionNormalTangentTexture with the given attributes.
         * @param position          The vertex position in object space.
         * @param normal            The surface normal at this vertex.
         * @param tangent           The surface tangent (xyz) and bitangent handedness sign (w).
         * @param textureCoordinate The UV texture coordinate.
         */
        VertexPositionNormalTangentTexture(const Microsoft::Xna::Framework::Vector3& position,
                                            const Microsoft::Xna::Framework::Vector3& normal,
                                            const Microsoft::Xna::Framework::Vector4& tangent,
                                            const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), Normal(normal), Tangent(tangent), TextureCoordinate(textureCoordinate)
        {
        }

        /**
         * @brief Returns the static vertex declaration describing the layout of this vertex type.
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTangentTexture.
         */
        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance (delegates to the static version).
         * @return Const reference to the VertexDeclaration for VertexPositionNormalTangentTexture.
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
        bool operator==(const VertexPositionNormalTangentTexture& o) const
        {
            return Position == o.Position && Normal == o.Normal
                && Tangent == o.Tangent && TextureCoordinate == o.TextureCoordinate;
        }
        /**
         * @brief Returns true if both vertices are not equal.
         * @param o The other vertex to compare with.
         * @return True if any field differs.
         */
        bool operator!=(const VertexPositionNormalTangentTexture& o) const { return !(*this == o); }
        /**
         * @brief Returns true if the given vertex equals this vertex.
         * @param other The other vertex to compare.
         * @return True if all fields are equal.
         */
        [[nodiscard]] bool Equals(const VertexPositionNormalTangentTexture& other) const { return *this == other; }
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
