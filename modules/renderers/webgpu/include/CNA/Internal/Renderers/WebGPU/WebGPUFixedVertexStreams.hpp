// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Renderers::WebGPU
{
    /** @brief Fixed shader family whose canonical record is requested. */
    enum class FixedVertexFamilyEXT { Skinned, Pbr, SkinnedPbr };

    /** @brief One per-vertex source, with effective (collision-remapped) semantics. */
    struct FixedVertexStreamEXT
    {
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>* elements = nullptr;
        const std::vector<std::uint8_t>* bytes = nullptr;
        std::size_t stride = 0;
        std::size_t vertexOffset = 0;
    };

    /** @brief Canonical bytes owned by the queued draw, independent of source lifetime. */
    struct FixedVertexDataEXT
    {
        std::vector<std::uint8_t> bytes;
        std::size_t stride = 0;
    };

    /**
     * @brief Copies consumed inputs across streams into one fixed shader record.
     * @param streams Per-vertex streams only; semantics have already been remapped by the device.
     * @param family Shader record to produce (52/56, 48/60 or 68/80 bytes).
     * @return Owned records after each binding's residual element offset. The caller still applies
     * vertexStart or baseVertex once; unused streams do not limit the record count.
     * @throws std::invalid_argument A required input, format or consumed byte range is invalid.
     */
    [[nodiscard]] FixedVertexDataEXT NormalizeFixedVertexStreamsEXT(
        std::span<const FixedVertexStreamEXT> streams, FixedVertexFamilyEXT family);
}
