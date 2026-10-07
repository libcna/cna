// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Metal/MetalPipelineKey.hpp"
#include "CNA/Internal/Renderers/Metal/MetalVertexAttribFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// plans/plan_apple_m4.md AM4-035: the vertex input of every built-in Metal pipeline, derived from
// the VertexDeclaration the game supplied instead of from the record stride alone. Each pipeline
// kind's vertex function reads a fixed list of semantics, one per [[attribute(i)]]; this header
// finds each semantic in the declaration (by usage and usage index), so a custom vertex -- extra
// channels, another order, another stride -- feeds the same shaders the canonical XNA vertex
// types do. Pure C++ (no Objective-C), so the mapping is unit-tested on any platform.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief One input a built-in Metal vertex function reads. */
    enum class MetalVertexSemantic : std::uint8_t
    {
        /** @brief POSITION0. */
        Position,
        /** @brief COLOR0. */
        Color,
        /** @brief TEXCOORD0. */
        TexCoord0,
        /** @brief TEXCOORD1; DualTextureEffect's second set. */
        TexCoord1,
        /** @brief NORMAL0. */
        Normal,
        /** @brief TANGENT0. */
        Tangent,
        /** @brief BLENDWEIGHT0. */
        BlendWeight,
        /** @brief BLENDINDICES0. */
        BlendIndices,
    };

    /** @brief The semantics a pipeline kind's vertex function reads, in attribute order. */
    struct MetalPipelineVertexInputs
    {
        /** @brief Semantic read by attribute `i`. */
        std::array<MetalVertexSemantic, 6> semantics{};
        /** @brief Number of attributes in use. */
        std::size_t count = 0;
    };

    /**
     * @brief Returns the semantics a built-in pipeline kind's vertex function reads.
     *
     * @param kind Pipeline kind.
     * @return The attribute-ordered semantics; empty for Sprite2D, which has no vertex descriptor.
     */
    [[nodiscard]] constexpr MetalPipelineVertexInputs MetalVertexInputsFor(MetalPipelineKind kind) noexcept
    {
        using S = MetalVertexSemantic;
        using K = MetalPipelineKind;
        switch (kind)
        {
            case K::Colored16:          return {{S::Position, S::Color}, 2};
            case K::Textured20:         return {{S::Position, S::TexCoord0}, 2};
            case K::ColorTex24:         return {{S::Position, S::Color, S::TexCoord0}, 3};
            case K::LitTex32:
            case K::LitTex32VertexLit:
            case K::EnvMap32:           return {{S::Position, S::Normal, S::TexCoord0}, 3};
            case K::DualTex20:          return {{S::Position, S::TexCoord0, S::TexCoord1}, 3};
            case K::DualTex24Colored:   return {{S::Position, S::Color, S::TexCoord0, S::TexCoord1}, 4};
            case K::Skinned52:
            case K::Skinned52VertexLit: return {{S::Position, S::Normal, S::TexCoord0, S::BlendWeight, S::BlendIndices}, 5};
            case K::Skinned56:
            case K::Skinned56VertexLit: return {{S::Position, S::Normal, S::TexCoord0, S::BlendWeight, S::BlendIndices, S::Color}, 6};
            case K::Pbr48:              return {{S::Position, S::Normal, S::Tangent, S::TexCoord0}, 4};
            case K::SkinnedPbr68:       return {{S::Position, S::Normal, S::Tangent, S::TexCoord0, S::BlendWeight, S::BlendIndices}, 6};
            case K::Sprite2D:           return {};
        }
        return {};
    }

    /** @brief One attribute of a Metal vertex descriptor, before it becomes Objective-C. */
    struct MetalDeclaredAttribute
    {
        /** @brief `[[attribute(location)]]` it feeds. */
        int location = 0;
        /** @brief Native attribute format. */
        MetalVertexAttribKind kind = MetalVertexAttribKind::Float3;
        /** @brief Byte offset inside one record. */
        int offset = 0;

        /**
         * @brief Compares every field.
         *
         * @param other Attribute to compare.
         * @return True when both describe the same binding.
         */
        [[nodiscard]] bool operator==(const MetalDeclaredAttribute& other) const noexcept
        {
            return location == other.location && kind == other.kind && offset == other.offset;
        }
    };

    /** @brief A complete vertex input for one pipeline kind, or the reason there is none. */
    struct MetalDeclaredVertexInput
    {
        /** @brief Attributes in location order. */
        std::vector<MetalDeclaredAttribute> attributes;
        /** @brief Record stride in bytes. */
        int stride = 0;
        /** @brief Why the declaration cannot feed the pipeline; empty when it can. */
        std::string refusal;

        /**
         * @brief Whether every attribute the pipeline reads was found.
         *
         * @return True when the input can be bound.
         */
        [[nodiscard]] bool IsComplete() const noexcept { return refusal.empty(); }

        /**
         * @brief A stable 64-bit identity of the layout, for the pipeline cache key.
         *
         * @return FNV-1a over the stride and every attribute; never zero.
         */
        [[nodiscard]] std::uint64_t LayoutKey() const noexcept
        {
            std::uint64_t h = 1469598103934665603ull;
            const auto mix = [&h](std::uint64_t value)
            {
                for (int i = 0; i < 8; ++i)
                {
                    h ^= (value >> (i * 8)) & 0xFFu;
                    h *= 1099511628211ull;
                }
            };
            mix(static_cast<std::uint64_t>(stride));
            for (const auto& a : attributes)
            {
                mix(static_cast<std::uint64_t>(a.location));
                mix(static_cast<std::uint64_t>(a.kind));
                mix(static_cast<std::uint64_t>(a.offset));
            }
            return h == 0 ? 1 : h;
        }
    };

    /**
     * @brief Byte size of one XNA vertex element format.
     *
     * @param format Element format.
     * @return Its size in bytes.
     */
    [[nodiscard]] constexpr int MetalVertexElementByteSize(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format) noexcept
    {
        using VEF = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        switch (format)
        {
            case VEF::Single:           return 4;
            case VEF::Vector2:          return 8;
            case VEF::Vector3:          return 12;
            case VEF::Vector4:          return 16;
            case VEF::Color:            return 4;
            case VEF::Byte4:            return 4;
            case VEF::Short2:           return 4;
            case VEF::Short4:           return 8;
            case VEF::NormalizedShort2: return 4;
            case VEF::NormalizedShort4: return 8;
            case VEF::HalfVector2:      return 4;
            case VEF::HalfVector4:      return 8;
        }
        return 0;
    }

    /**
     * @brief The usage and usage index that supply a semantic.
     *
     * @param semantic Shader input.
     * @param usage Receives the XNA usage.
     * @param usageIndex Receives the XNA usage index.
     */
    constexpr void MetalSemanticUsage(MetalVertexSemantic semantic,
                                      Microsoft::Xna::Framework::Graphics::VertexElementUsage& usage,
                                      int& usageIndex) noexcept
    {
        using U = Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        usageIndex = 0;
        switch (semantic)
        {
            case MetalVertexSemantic::Position:     usage = U::Position; break;
            case MetalVertexSemantic::Color:        usage = U::Color; break;
            case MetalVertexSemantic::TexCoord0:    usage = U::TextureCoordinate; break;
            case MetalVertexSemantic::TexCoord1:    usage = U::TextureCoordinate; usageIndex = 1; break;
            case MetalVertexSemantic::Normal:       usage = U::Normal; break;
            case MetalVertexSemantic::Tangent:      usage = U::Tangent; break;
            case MetalVertexSemantic::BlendWeight:  usage = U::BlendWeight; break;
            case MetalVertexSemantic::BlendIndices: usage = U::BlendIndices; break;
        }
    }

    /**
     * @brief Whether an element format can feed a semantic's shader type.
     *
     * Metal converts a float, half or normalized format to the shader's float vector, but not a
     * raw integer format; the skinning shaders read BLENDINDICES as `uchar4`, which only Byte4
     * supplies.
     *
     * @param semantic Shader input.
     * @param format Element format.
     * @return True when Metal can bind the format to that input.
     */
    [[nodiscard]] constexpr bool MetalSemanticAcceptsFormat(
        MetalVertexSemantic semantic,
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format) noexcept
    {
        using VEF = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        if (semantic == MetalVertexSemantic::BlendIndices)
            return format == VEF::Byte4;
        switch (format)
        {
            case VEF::Byte4:
            case VEF::Short2:
            case VEF::Short4:
                return false;
            default:
                return true;
        }
    }

    namespace MetalDeclaredVertexInputDetail
    {
        [[nodiscard]] inline const char* SemanticName(MetalVertexSemantic semantic) noexcept
        {
            switch (semantic)
            {
                case MetalVertexSemantic::Position:     return "Position0";
                case MetalVertexSemantic::Color:        return "Color0";
                case MetalVertexSemantic::TexCoord0:    return "TextureCoordinate0";
                case MetalVertexSemantic::TexCoord1:    return "TextureCoordinate1";
                case MetalVertexSemantic::Normal:       return "Normal0";
                case MetalVertexSemantic::Tangent:      return "Tangent0";
                case MetalVertexSemantic::BlendWeight:  return "BlendWeight0";
                case MetalVertexSemantic::BlendIndices: return "BlendIndices0";
            }
            return "?";
        }

        [[nodiscard]] inline const Microsoft::Xna::Framework::Graphics::VertexElement* Find(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
            MetalVertexSemantic semantic) noexcept
        {
            Microsoft::Xna::Framework::Graphics::VertexElementUsage usage{};
            int usageIndex = 0;
            MetalSemanticUsage(semantic, usage, usageIndex);
            for (const auto& e : elements)
                if (e.getVertexElementUsageProperty() == usage && e.getUsageIndexProperty() == usageIndex)
                    return &e;
            return nullptr;
        }
    }

    /**
     * @brief Maps a declaration onto the attributes a pipeline kind's vertex function reads.
     *
     * Every semantic is looked up by usage and usage index; elements the pipeline does not read are
     * ignored, as XNA ignores vertex channels the bound effect does not consume. One aliasing rule,
     * VULKAN-150's: DualTextureEffect samples its second texture with TEXCOORD1, and a record that
     * declares only TEXCOORD0 -- every VertexPositionTexture and VertexPositionColorTexture -- feeds
     * TEXCOORD0 to both, which is the only set it has. Nothing else is invented.
     *
     * @param kind Pipeline kind the draw selected.
     * @param elements The declaration's elements.
     * @param recordStride Bytes per record in the bound buffer.
     * @return The complete input, or one carrying the refusal reason.
     */
    [[nodiscard]] inline MetalDeclaredVertexInput BuildMetalDeclaredVertexInput(
        MetalPipelineKind kind,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        int recordStride)
    {
        using namespace MetalDeclaredVertexInputDetail;
        MetalDeclaredVertexInput input;
        input.stride = recordStride;
        const MetalPipelineVertexInputs inputs = MetalVertexInputsFor(kind);
        for (std::size_t location = 0; location < inputs.count; ++location)
        {
            const MetalVertexSemantic semantic = inputs.semantics[location];
            const auto* element = Find(elements, semantic);
            if (!element && semantic == MetalVertexSemantic::TexCoord1)
                element = Find(elements, MetalVertexSemantic::TexCoord0);
            if (!element)
            {
                input.refusal = std::string("the declaration has no ") + SemanticName(semantic) +
                                ", which this effect's vertex function reads";
                return input;
            }
            const auto format = element->getVertexElementFormatProperty();
            if (!MetalSemanticAcceptsFormat(semantic, format))
            {
                input.refusal = std::string(SemanticName(semantic)) +
                                " has an integer format Metal cannot convert to the shader's input";
                return input;
            }
            const int offset = element->getOffsetProperty();
            if (offset < 0 || offset + MetalVertexElementByteSize(format) > recordStride)
            {
                input.refusal = std::string(SemanticName(semantic)) +
                                " lies outside the " + std::to_string(recordStride) + "-byte record";
                return input;
            }
            input.attributes.push_back(MetalDeclaredAttribute{
                static_cast<int>(location), DescribeMetalVertexElementFormat(format).kind, offset});
        }
        return input;
    }

    /**
     * @brief The XNA vertex type each kind's canonical stride stands for, as a declaration.
     *
     * Used for a buffer that never received a declaration (the stride is then the only
     * description there is), so it takes exactly the descriptor the fixed stride table built.
     *
     * @param kind Pipeline kind.
     * @return The canonical elements; empty for Sprite2D.
     */
    [[nodiscard]] inline std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>
    MetalCanonicalElementsFor(MetalPipelineKind kind)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using F = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using U = Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        using K = MetalPipelineKind;
        const VertexElement position(0, F::Vector3, U::Position, 0);
        switch (kind)
        {
            case K::Colored16:
                return {position, VertexElement(12, F::Color, U::Color, 0)};
            case K::Textured20:
            case K::DualTex20:
                return {position, VertexElement(12, F::Vector2, U::TextureCoordinate, 0)};
            case K::ColorTex24:
            case K::DualTex24Colored:
                return {position, VertexElement(12, F::Color, U::Color, 0),
                        VertexElement(16, F::Vector2, U::TextureCoordinate, 0)};
            case K::LitTex32:
            case K::LitTex32VertexLit:
            case K::EnvMap32:
                return {position, VertexElement(12, F::Vector3, U::Normal, 0),
                        VertexElement(24, F::Vector2, U::TextureCoordinate, 0)};
            case K::Skinned52:
            case K::Skinned52VertexLit:
                return {position, VertexElement(12, F::Vector3, U::Normal, 0),
                        VertexElement(24, F::Vector2, U::TextureCoordinate, 0),
                        VertexElement(32, F::Vector4, U::BlendWeight, 0),
                        VertexElement(48, F::Byte4, U::BlendIndices, 0)};
            case K::Skinned56:
            case K::Skinned56VertexLit:
                return {position, VertexElement(12, F::Vector3, U::Normal, 0),
                        VertexElement(24, F::Vector2, U::TextureCoordinate, 0),
                        VertexElement(32, F::Vector4, U::BlendWeight, 0),
                        VertexElement(48, F::Byte4, U::BlendIndices, 0),
                        VertexElement(52, F::Color, U::Color, 0)};
            case K::Pbr48:
                return {position, VertexElement(12, F::Vector3, U::Normal, 0),
                        VertexElement(24, F::Vector4, U::Tangent, 0),
                        VertexElement(40, F::Vector2, U::TextureCoordinate, 0)};
            case K::SkinnedPbr68:
                return {position, VertexElement(12, F::Vector3, U::Normal, 0),
                        VertexElement(24, F::Vector4, U::Tangent, 0),
                        VertexElement(40, F::Vector2, U::TextureCoordinate, 0),
                        VertexElement(48, F::Vector4, U::BlendWeight, 0),
                        VertexElement(64, F::Byte4, U::BlendIndices, 0)};
            case K::Sprite2D:
                return {};
        }
        return {};
    }

    /**
     * @brief The canonical stride to select a pipeline with, for a declared vertex.
     *
     * SelectMetalPipelineKind keeps the one precedence table, keyed by the canonical stride of each
     * vertex shape; a declared vertex is mapped onto the shape its channels and the effect's flags
     * call for. The effect family decides first, as in SelectMetalPipelineKind; then a BasicEffect-
     * family draw is lit-capable when the vertex has a normal (the stride-32 route, whose lighting-
     * off case degenerates exactly as it always has), else colour-and-texture, texture, or colour.
     *
     * @param elements The declaration's elements.
     * @param params Draw parameters; null for the legacy coloured route.
     * @return A canonical stride for SelectMetalPipelineKind.
     */
    [[nodiscard]] inline std::size_t MetalSelectionStrideForDeclaration(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        const CNA::Internal::Renderers::GpuDrawParams* params)
    {
        using namespace MetalDeclaredVertexInputDetail;
        const bool color = Find(elements, MetalVertexSemantic::Color) != nullptr;
        const bool uv = Find(elements, MetalVertexSemantic::TexCoord0) != nullptr;
        const bool normal = Find(elements, MetalVertexSemantic::Normal) != nullptr;
        if (!params) return 16;
        if (params->pbr) return params->skinned ? 68 : 48;
        if (params->skinned) return color ? 56 : 52;
        if (params->envMapping) return 32;
        if (params->dualTexture) return color ? 24 : 20;
        if (normal) return 32;
        if (color && uv) return 24;
        if (uv) return 20;
        return 16;
    }

    /**
     * @brief Why a BasicEffect-family draw cannot honour VertexColorEnabled on this pipeline.
     *
     * A lit BasicEffect over a vertex that also carries COLOR0 multiplies by it on XNA; Metal's
     * lit vertex functions read no colour, so drawing would silently drop it. Refused instead.
     * Only the BasicEffect family is judged: the other stock effects either read colour themselves
     * or have no VertexColorEnabled.
     *
     * @param kind Pipeline kind the draw selected.
     * @param elements The declaration's elements.
     * @param params Draw parameters; null for the legacy coloured route.
     * @return The refusal reason, or empty when nothing active is dropped.
     */
    [[nodiscard]] inline std::string MetalDroppedVertexColorRefusal(
        MetalPipelineKind kind,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        const CNA::Internal::Renderers::GpuDrawParams* params)
    {
        if (!params || params->pbr || params->skinned || params->envMapping || params->dualTexture)
            return {};
        if (!params->vertexColorEnabled) return {};
        if (MetalDeclaredVertexInputDetail::Find(elements, MetalVertexSemantic::Color) == nullptr)
            return {};
        const MetalPipelineVertexInputs inputs = MetalVertexInputsFor(kind);
        for (std::size_t i = 0; i < inputs.count; ++i)
            if (inputs.semantics[i] == MetalVertexSemantic::Color) return {};
        return "VertexColorEnabled with a Color0 channel on a lit BasicEffect: Metal's lit vertex "
               "functions read no vertex colour";
    }
}
