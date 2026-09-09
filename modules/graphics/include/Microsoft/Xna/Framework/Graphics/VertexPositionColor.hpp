// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <string>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
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
    struct VertexPositionColor : public IVertexType
    {
        /** @brief Position in object space. */
        Microsoft::Xna::Framework::Vector3 Position;
        /** @brief Per-vertex color. */
        Microsoft::Xna::Framework::Color Color;

        /** @brief Constructs a default VertexPositionColor with zero-initialized fields. */
        VertexPositionColor() = default;

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
         *
         * The declaration describes the packed GPU vertex stream — a 16-byte stride carrying a
         * float3 position and a colour packed as four bytes — not the C++ object, whose size also
         * covers the vtable `Color` brings in and the padding its alignment forces.
         *
         * @return Const reference to the VertexDeclaration for VertexPositionColor.
         */
        [[nodiscard]] static const ::Microsoft::Xna::Framework::Graphics::VertexDeclaration& getVertexDeclarationStatic();

        /**
         * @brief Returns the vertex declaration for this instance.
         * @return The static VertexPositionColor declaration.
         */
        [[nodiscard]] const VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        /**
         * @brief Tests equality by comparing Position and Color.
         * @param left  Left operand.
         * @param right Right operand.
         * @return True if Position and Color are equal.
         */
        [[nodiscard]] friend bool operator==(const VertexPositionColor& left,
                                             const VertexPositionColor& right)
        {
            return left.Color == right.Color && left.Position == right.Position;
        }

        /**
         * @brief Tests inequality.
         * @param left  Left operand.
         * @param right Right operand.
         * @return True if any field differs.
         */
        [[nodiscard]] friend bool operator!=(const VertexPositionColor& left,
                                             const VertexPositionColor& right)
        {
            return !(left == right);
        }

        /**
         * @brief Compares this vertex to another for equality.
         * @param other The vertex to compare against.
         * @return True if Position and Color are equal.
         */
        [[nodiscard]] bool Equals(const VertexPositionColor& other) const { return *this == other; }

        /**
         * @brief Returns a hash code derived from this vertex's fields.
         * @return The hash code.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns a human-readable description of this vertex.
         * @return String of the form "{{Position:... Color:...}}".
         */
        [[nodiscard]] std::string ToString() const;
    };
}
