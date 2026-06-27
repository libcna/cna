// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <string>

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
         * @brief Returns a hash code. Consistent with FNA (always 0).
         * @return 0.
         */
        [[nodiscard]] std::size_t GetHashCode() const { return 0; }

        /**
         * @brief Returns a human-readable description of this vertex.
         * @return String of the form "{{Position:... Color:...}}".
         */
        [[nodiscard]] std::string ToString() const;
    };
}
