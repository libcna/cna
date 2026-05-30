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
        /** Byte offset from the start of the vertex. */
        int Offset = 0;
        /** Component type / count of this attribute. */
        VertexElementFormat VertexElementFormatValue = VertexElementFormat::Single;
        /** Semantic channel of this attribute. */
        VertexElementUsage VertexElementUsageValue = VertexElementUsage::Position;
        /** Index for repeated semantics (e.g. multiple texcoords). */
        int UsageIndex = 0;

        VertexElement() = default;

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
