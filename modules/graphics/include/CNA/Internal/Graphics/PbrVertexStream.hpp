// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/StockVertexSemantics.hpp"
#include <cstring>
#include <limits>

namespace CNA::Internal::Graphics
{
    /**
     * @brief Maps a declared rigid PBR stream to the existing 48/60-byte shader
     * records. Missing tangents use the same neutral (0,0,0,1) input as the GL
     * renderer. Canonical streams retain their resident GPU storage; only differing
     * layouts are copied.
     * @param elements Source declaration (empty means the caller's legacy canonical
     * layout).
     * @param source Source vertex bytes.
     * @param stride Source stride, replaced with the canonical stride on success.
     * @param normalized Receives rewritten bytes, or remains empty for a canonical
     * stream.
     * @return True if the declaration can be represented without losing consumed
     * semantics.
     */
    [[nodiscard]] inline bool NormalizeRigidPbrStreamEXT(const std::vector<VertexElement>& elements,
                                                         const std::vector<std::uint8_t>& source,
                                                         std::size_t& stride,
                                                         std::vector<std::uint8_t>& normalized)
    {
        if (elements.empty())
            return stride == 48 || stride == 60;
        if (stride == 0 || source.size() % stride != 0)
            return false;
        const auto find = [&](VertexElementUsage usage, int index = 0) {
            return FindDeclaredSemanticEXT(elements, usage, index);
        };
        const auto* position = find(VertexElementUsage::Position);
        const auto* normal = find(VertexElementUsage::Normal);
        const auto* tangent = find(VertexElementUsage::Tangent);
        const auto* uv = find(VertexElementUsage::TextureCoordinate);
        const auto* uv1 = find(VertexElementUsage::TextureCoordinate, 1);
        const auto* color = find(VertexElementUsage::Color);
        const auto valid = [&](const VertexElement* element, VertexElementFormat format, std::size_t size) {
            return element && element->getVertexElementFormatProperty() == format &&
                   element->getOffsetProperty() >= 0 &&
                   static_cast<std::size_t>(element->getOffsetProperty()) <= stride &&
                   size <= stride - static_cast<std::size_t>(element->getOffsetProperty());
        };
        if (!valid(position, VertexElementFormat::Vector3, 12) ||
            !valid(normal, VertexElementFormat::Vector3, 12) || !valid(uv, VertexElementFormat::Vector2, 8) ||
            (tangent && !valid(tangent, VertexElementFormat::Vector4, 16)) ||
            (uv1 && !valid(uv1, VertexElementFormat::Vector2, 8)) ||
            (color && !valid(color, VertexElementFormat::Color, 4)))
            return false;

        // The extended record carries both UV1 and COLOR0. White preserves the
        // uncolored material when UV1 alone requires this variant; absent UV1
        // defaults as in GL.
        const std::size_t targetStride = color || uv1 ? 60 : 48;
        if (stride == targetStride && position->getOffsetProperty() == 0 &&
            normal->getOffsetProperty() == 12 && tangent && tangent->getOffsetProperty() == 24 &&
            uv->getOffsetProperty() == 40 &&
            (targetStride == 48 ||
             (uv1 && uv1->getOffsetProperty() == 48 && color && color->getOffsetProperty() == 56)))
            return true;
        const std::size_t count = source.size() / stride;
        if (count > std::numeric_limits<std::size_t>::max() / targetStride)
            return false;
        normalized.assign(count * targetStride, 0);
        for (std::size_t vertex = 0; vertex < count; ++vertex)
        {
            auto* dst = normalized.data() + vertex * targetStride;
            const auto copy = [&](const VertexElement* element, std::size_t offset, std::size_t size) {
                if (element)
                    std::memcpy(dst + offset, source.data() + vertex * stride + element->getOffsetProperty(),
                                size);
            };
            copy(position, 0, 12);
            copy(normal, 12, 12);
            std::memcpy(dst + 24, kNeutralVertexRecordEXT.data(), 16);
            copy(tangent, 24, 16);
            copy(uv, 40, 8);
            if (targetStride == 60)
            {
                copy(uv1, 48, 8);
                std::memset(dst + 56, 255, 4);
                copy(color, 56, 4);
            }
        }
        stride = targetStride;
        return true;
    }
} // namespace CNA::Internal::Graphics
