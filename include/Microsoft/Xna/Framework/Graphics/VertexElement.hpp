// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief XNA 4.0 `VertexElement`.
     *
     * Describes a single attribute slot inside a vertex stride.
     *
     * @note Status: STUB. Stored verbatim and exposed via
     *       `VertexDeclaration::GetVertexElements()` for API-shape
     *       compatibility; the EasyGL backend currently still routes
     *       through hard-coded layouts for the built-in vertex types.
     */
    struct VertexElement
    {
        /** @brief Byte offset from the start of the vertex stride. */
        int Offset = 0;
        /** @brief Component type and count of this vertex attribute. */
        VertexElementFormat VertexElementFormatValue = VertexElementFormat::Single;
        /** @brief Semantic channel (e.g. Position, Color, TextureCoordinate) of this attribute. */
        VertexElementUsage VertexElementUsageValue = VertexElementUsage::Position;
        /** @brief Index for repeated semantics (e.g. multiple texture coordinate sets). */
        int UsageIndex = 0;

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
            : Offset(offset),
              VertexElementFormatValue(format),
              VertexElementUsageValue(usage),
              UsageIndex(usageIndex)
        {
        }
    };
}
