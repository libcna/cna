// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Describes a single attribute slot inside a vertex stride.
     *
     * Maps to the XNA VertexElement struct. Holds the byte offset, data format,
     * semantic usage, and usage index for one element within a vertex declaration.
     */
    struct VertexElement
    {
        // ── Constructors ────────────────────────────────────────────────────

        /** @brief Constructs a default VertexElement with offset 0, Single format, and Position usage. */
        VertexElement() = default;

        /**
         * @brief Constructs a VertexElement with the specified offset, format, usage, and usage index.
         * @param offset     Byte offset from the start of the vertex.
         * @param format     Data format and component count for this attribute.
         * @param usage      Semantic meaning (Position, Color, TextureCoordinate, etc.).
         * @param usageIndex Disambiguates repeated semantics (e.g. TEXCOORD0 vs TEXCOORD1).
         */
        VertexElement(int offset,
                      VertexElementFormat format,
                      VertexElementUsage usage,
                      int usageIndex)
            : offset_(offset),
              vertexElementFormat_(format),
              vertexElementUsage_(usage),
              usageIndex_(usageIndex)
        {
        }

        // ── Properties ──────────────────────────────────────────────────────

        /**
         * @brief Returns the byte offset from the start of the vertex stride.
         * @return Offset in bytes.
         */
        [[nodiscard]] int getOffsetProperty() const { return offset_; }

        /**
         * @brief Sets the byte offset from the start of the vertex stride.
         * @param value Offset in bytes.
         */
        void setOffsetProperty(int value) { offset_ = value; }

        /**
         * @brief Returns the data format and component count of this vertex attribute.
         * @return The VertexElementFormat value.
         */
        [[nodiscard]] VertexElementFormat getVertexElementFormatProperty() const { return vertexElementFormat_; }

        /**
         * @brief Sets the data format and component count of this vertex attribute.
         * @param value The VertexElementFormat value.
         */
        void setVertexElementFormatProperty(VertexElementFormat value) { vertexElementFormat_ = value; }

        /**
         * @brief Returns the semantic usage of this vertex attribute.
         * @return The VertexElementUsage value.
         */
        [[nodiscard]] VertexElementUsage getVertexElementUsageProperty() const { return vertexElementUsage_; }

        /**
         * @brief Sets the semantic usage of this vertex attribute.
         * @param value The VertexElementUsage value.
         */
        void setVertexElementUsageProperty(VertexElementUsage value) { vertexElementUsage_ = value; }

        /**
         * @brief Returns the usage index, disambiguating repeated semantics.
         * @return Usage index (e.g. 0 for TEXCOORD0, 1 for TEXCOORD1).
         */
        [[nodiscard]] int getUsageIndexProperty() const { return usageIndex_; }

        /**
         * @brief Sets the usage index.
         * @param value Usage index.
         */
        void setUsageIndexProperty(int value) { usageIndex_ = value; }

        // ── Equality ────────────────────────────────────────────────────────

        /**
         * @brief Tests equality between two VertexElements.
         * @param left  Left operand.
         * @param right Right operand.
         * @return True if all four fields are equal.
         */
        [[nodiscard]] friend bool operator==(const VertexElement& left, const VertexElement& right)
        {
            return left.offset_              == right.offset_
                && left.usageIndex_          == right.usageIndex_
                && left.vertexElementUsage_  == right.vertexElementUsage_
                && left.vertexElementFormat_ == right.vertexElementFormat_;
        }

        /**
         * @brief Tests inequality between two VertexElements.
         * @param left  Left operand.
         * @param right Right operand.
         * @return True if any field differs.
         */
        [[nodiscard]] friend bool operator!=(const VertexElement& left, const VertexElement& right)
        {
            return !(left == right);
        }

        /**
         * @brief Compares this element to another for equality.
         * @param other The element to compare against.
         * @return True if all four fields are equal.
         */
        [[nodiscard]] bool Equals(const VertexElement& other) const { return *this == other; }

        // ── Hashing / string ────────────────────────────────────────────────

        /**
         * @brief Returns a hash code for this element.
         * @return 0 (consistent with FNA's TODO implementation).
         */
        [[nodiscard]] std::size_t GetHashCode() const { return 0; }

        /**
         * @brief Returns a human-readable description of this element.
         * @return String of the form "{{Offset:N Format:F Usage:U UsageIndex:I}}".
         */
        [[nodiscard]] std::string ToString() const;

    private:
        int                 offset_              = 0;
        VertexElementFormat vertexElementFormat_ = VertexElementFormat::Single;
        VertexElementUsage  vertexElementUsage_  = VertexElementUsage::Position;
        int                 usageIndex_          = 0;
    };
}
