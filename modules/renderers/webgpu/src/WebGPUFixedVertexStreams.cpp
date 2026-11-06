// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/WebGPU/WebGPUFixedVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Renderers::WebGPU
{
    using namespace Microsoft::Xna::Framework::Graphics;

    FixedVertexDataEXT NormalizeFixedVertexStreamsEXT(
        std::span<const FixedVertexStreamEXT> streams, FixedVertexFamilyEXT family)
    {
        struct Input
        {
            VertexElementUsage usage;
            int index;
            VertexElementFormat format;
            std::size_t size, target;
            bool required;
            const FixedVertexStreamEXT* source = nullptr;
            const VertexElement* element = nullptr;
        };
        const bool pbr = family != FixedVertexFamilyEXT::Skinned;
        const bool skin = family != FixedVertexFamilyEXT::Pbr;
        const std::size_t baseStride = pbr ? (skin ? 68 : 48) : 52;
        std::vector<Input> inputs{
            {VertexElementUsage::Position, 0, VertexElementFormat::Vector3, 12, 0, true},
            {VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, 12, 12, true},
            {VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, 8,
             pbr ? 40u : 24u, true}};
        if (pbr)
        {
            inputs.push_back({VertexElementUsage::Tangent, 0, VertexElementFormat::Vector4,
                              16, 24, false});
            inputs.push_back({VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2,
                              8, baseStride, false});
        }
        if (skin)
        {
            inputs.push_back({VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4,
                              16, pbr ? 48u : 32u, true});
            inputs.push_back({VertexElementUsage::BlendIndices, 0, VertexElementFormat::Byte4,
                              4, pbr ? 64u : 48u, true});
        }
        inputs.push_back({VertexElementUsage::Color, 0, VertexElementFormat::Color,
                          4, pbr ? baseStride + 8 : baseStride, false});

        auto invalid = [] { throw std::invalid_argument(
            "CNA WebGPU: fixed vertex streams have a missing, ambiguous or invalid consumed input"); };
        std::size_t count = std::numeric_limits<std::size_t>::max();
        bool extended = false;
        for (auto& input : inputs)
        {
            for (const auto& stream : streams)
            {
                if (!stream.elements) continue;
                for (const auto& element : *stream.elements)
                {
                    if (element.getVertexElementUsageProperty() != input.usage ||
                        element.getUsageIndexProperty() != input.index) continue;
                    if (input.element) invalid();
                    input.source = &stream;
                    input.element = &element;
                }
            }
            if (!input.element)
            {
                if (input.required) invalid();
                continue;
            }
            const auto format = input.element->getVertexElementFormatProperty();
            const bool floatIndices = input.usage == VertexElementUsage::BlendIndices &&
                                      format == VertexElementFormat::Vector4;
            const std::size_t size = floatIndices ? 16 : input.size;
            const auto& source = *input.source;
            const int offset = input.element->getOffsetProperty();
            if ((format != input.format && !floatIndices) || !source.bytes || source.stride == 0 ||
                offset < 0 || static_cast<std::size_t>(offset) > source.stride ||
                size > source.stride - static_cast<std::size_t>(offset) ||
                source.bytes->size() % source.stride != 0 ||
                source.vertexOffset > source.bytes->size() / source.stride) invalid();
            count = std::min(count, source.bytes->size() / source.stride - source.vertexOffset);
            if (input.usage == VertexElementUsage::Color ||
                (input.usage == VertexElementUsage::TextureCoordinate && input.index == 1))
                extended = true;
        }
        FixedVertexDataEXT out;
        out.stride = baseStride + (extended ? (pbr ? 12 : 4) : 0);
        if (count == std::numeric_limits<std::size_t>::max() ||
            count > std::numeric_limits<std::size_t>::max() / out.stride) invalid();
        out.bytes.assign(count * out.stride, 0);
        for (std::size_t vertex = 0; vertex < count; ++vertex)
        {
            auto* target = out.bytes.data() + vertex * out.stride;
            if (pbr)
            {
                const float one = 1;
                std::memcpy(target + 36, &one, 4); // Absent tangent: (0,0,0,1).
            }
            if (extended) std::memset(target + (pbr ? baseStride + 8 : baseStride), 255, 4);
            for (const auto& input : inputs)
            {
                if (!input.element) continue;
                const auto& stream = *input.source;
                const auto* source = stream.bytes->data() +
                    (stream.vertexOffset + vertex) * stream.stride + input.element->getOffsetProperty();
                if (input.usage == VertexElementUsage::BlendIndices &&
                    input.element->getVertexElementFormatProperty() == VertexElementFormat::Vector4)
                {
                    std::array<float, 4> indices;
                    std::memcpy(indices.data(), source, 16);
                    for (std::size_t lane = 0; lane < indices.size(); ++lane)
                    {
                        if (!std::isfinite(indices[lane])) invalid();
                        // Preserve the established single-stream conversion for finite inputs.
                        target[input.target + lane] = static_cast<std::uint8_t>(
                            std::clamp(indices[lane], 0.0f, 255.0f) + 0.5f);
                    }
                }
                else std::memcpy(target + input.target, source, input.size);
            }
        }
        return out;
    }
}
