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
        std::array<MetalVertexSemantic, 8> semantics{};
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
            // plans/plan_apple_m4.md AM4-081: the lit BasicEffect functions read COLOR0 as well.
            case K::LitTex32:
            case K::LitTex32VertexLit:  return {{S::Position, S::Normal, S::TexCoord0, S::Color}, 4};
            case K::EnvMap32:           return {{S::Position, S::Normal, S::TexCoord0}, 3};
            case K::DualTex20:          return {{S::Position, S::TexCoord0, S::TexCoord1}, 3};
            case K::DualTex24Colored:   return {{S::Position, S::Color, S::TexCoord0, S::TexCoord1}, 4};
            case K::Skinned52:
            case K::Skinned52VertexLit: return {{S::Position, S::Normal, S::TexCoord0, S::BlendWeight, S::BlendIndices}, 5};
            case K::Skinned56:
            case K::Skinned56VertexLit: return {{S::Position, S::Normal, S::TexCoord0, S::BlendWeight, S::BlendIndices, S::Color}, 6};
            // plans/plan_apple_m4.md AM4-084/085: the PBR functions read glTF's COLOR_0, and TEXCOORD_1
            // for the maps the material puts on its second coordinate set.
            case K::Pbr48:              return {{S::Position, S::Normal, S::Tangent, S::TexCoord0, S::Color, S::TexCoord1}, 6};
            case K::SkinnedPbr68:       return {{S::Position, S::Normal, S::Tangent, S::TexCoord0, S::BlendWeight, S::BlendIndices, S::Color, S::TexCoord1}, 8};
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
        /** @brief Byte offset inside one record; inside the constant block when `constant`. */
        int offset = 0;
        /**
         * @brief Read from the constant block instead of the vertex record.
         *
         * plans/plan_apple_m4.md AM4-080: an input the vertex function declares but the active
         * effect permutation does not use (see StockEffectUsesVertexSemantic).
         */
        bool constant = false;
        /**
         * @brief Vertex-buffer argument index of the stream it reads (MetalVertexStreamBufferIndex).
         *
         * plans/plan_apple_m4.md AM4-143: 0 for a single-stream draw; ignored when `constant`.
         */
        int bufferIndex = 0;

        /**
         * @brief Compares every field.
         *
         * @param other Attribute to compare.
         * @return True when both describe the same binding.
         */
        [[nodiscard]] bool operator==(const MetalDeclaredAttribute& other) const noexcept
        {
            return location == other.location && kind == other.kind && offset == other.offset &&
                   constant == other.constant && bufferIndex == other.bufferIndex;
        }
    };

    /** @brief One vertex-buffer layout of a multi-stream input (plans/plan_apple_m4.md AM4-143). */
    struct MetalDeclaredLayout
    {
        /** @brief Vertex-buffer argument index the stream binds at. */
        int bufferIndex = 0;
        /** @brief The stream's own record stride in bytes. */
        int stride = 0;
        /** @brief 0 for a per-vertex stream; otherwise its InstanceFrequency (the per-instance step rate). */
        int stepRate = 0;

        /**
         * @brief Compares every field.
         *
         * @param other Layout to compare.
         * @return True when both describe the same layout.
         */
        [[nodiscard]] bool operator==(const MetalDeclaredLayout& other) const noexcept
        {
            return bufferIndex == other.bufferIndex && stride == other.stride && stepRate == other.stepRate;
        }
    };

    /**
     * @brief First `[[attribute]]` of the stock vertex functions' optional per-instance world matrix.
     *
     * plans/plan_apple_m4.md AM4-143: attributes 12..15 hold its four columns, EasyGL's locations
     * (REMED-GFX-122) and above every input a stock function reads.
     */
    inline constexpr int kMetalInstanceMatrixLocation = 12;

    /** @brief Buffer index of public stream slot 1; slot `s > 0` binds at this plus `s - 1`. */
    inline constexpr int kMetalVertexStreamBufferBase = 14;

    /**
     * @brief The vertex-buffer argument index a public vertex stream slot binds at.
     *
     * Slot 0 keeps index 0, where every single-stream draw has always bound, so that path is
     * unchanged; slots 1..15 take 14..28, clear of the uniforms (1-3) and the constant block (30).
     *
     * @param slot Public `SetVertexBuffers` slot, 0..15.
     * @return The Metal vertex-buffer argument index.
     */
    [[nodiscard]] constexpr int MetalVertexStreamBufferIndex(int slot) noexcept
    {
        return slot == 0 ? 0 : kMetalVertexStreamBufferBase + slot - 1;
    }

    /** @brief Vertex buffer index the constant block is bound at; above every built-in MSL slot. */
    inline constexpr int kMetalConstantAttributeBufferIndex = 30;

    /** @brief Byte offset of the all-zero vector inside the constant block. */
    inline constexpr int kMetalConstantAttributeZeroOffset = 0;

    /** @brief Byte offset of the all-one vector (opaque white) inside the constant block. */
    inline constexpr int kMetalConstantAttributeOneOffset = 16;

    /**
     * @brief Byte offset of (0, 0, 0, 1) inside the constant block -- what a GL attribute location
     *        without an array reads, and so an instance-matrix column no declaration supplies.
     */
    inline constexpr int kMetalConstantAttributeUnitWOffset = 32;

    /** @brief The constant block: a float4 of zeros, a float4 of ones, then (0, 0, 0, 1). */
    inline constexpr float kMetalConstantAttributeBlock[12] = {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1};

    /** @brief A complete vertex input for one pipeline kind, or the reason there is none. */
    struct MetalDeclaredVertexInput
    {
        /** @brief Attributes in location order. */
        std::vector<MetalDeclaredAttribute> attributes;
        /** @brief Record stride in bytes. */
        int stride = 0;
        /**
         * @brief Every stream's layout; empty for a single-stream draw, whose one layout is
         *        buffer 0 at `stride` (plans/plan_apple_m4.md AM4-143).
         */
        std::vector<MetalDeclaredLayout> layouts;
        /** @brief The draw supplies the per-instance world matrix (attributes 12..15). */
        bool instanceMatrix = false;
        /**
         * @brief BLENDINDICES is read through the shaders' float4 input, not the uchar4 one
         *        (plans/plan_apple_m4.md AM4-152).
         */
        bool boneIndicesFloat = false;
        /** @brief What the float4 bone-index input is multiplied by before its rounding. */
        float boneIndexScale = 1.0f;
        /**
         * @brief Per float semantic (MetalSemanticScaleSlot), the per-component factor its stage_in
         *        read is multiplied by and rounded with -- 1 everywhere except a raw integer element
         *        fetched normalized (plans/plan_apple_m4.md AM4-155; MetalIntegerElementScale).
         */
        std::array<std::array<float, 4>, 7> semanticScales = []{
            std::array<std::array<float, 4>, 7> ones{};
            for (auto& scale : ones) scale = {1.0f, 1.0f, 1.0f, 1.0f};
            return ones;
        }();
        /** @brief Why the declaration cannot feed the pipeline; empty when it can. */
        std::string refusal;

        /**
         * @brief Whether any attribute reads the constant block.
         *
         * @return True when the draw must bind kMetalConstantAttributeBlock.
         */
        [[nodiscard]] bool UsesConstantAttributes() const noexcept
        {
            for (const auto& a : attributes)
                if (a.constant) return true;
            return false;
        }

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
                mix(a.constant ? 1u : 0u);
                mix(static_cast<std::uint64_t>(a.bufferIndex));
            }
            for (const auto& l : layouts)
            {
                mix(static_cast<std::uint64_t>(l.bufferIndex));
                mix(static_cast<std::uint64_t>(l.stride));
                mix(static_cast<std::uint64_t>(l.stepRate));
            }
            mix(instanceMatrix ? 1u : 0u);
            mix(boneIndicesFloat ? 1u : 0u);
            mix(static_cast<std::uint64_t>(boneIndexScale));
            for (const auto& scale : semanticScales)
                for (const float factor : scale) mix(static_cast<std::uint64_t>(factor));
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

    /** @brief How a BLENDINDICES element reaches the skinning shaders (AM4-152). */
    struct MetalBoneIndexInput
    {
        /** @brief The vertex format Metal fetches the element as. */
        MetalVertexAttribKind kind = MetalVertexAttribKind::UChar4;
        /** @brief Read through the float4 input rather than the uchar4 one. */
        bool floatInput = false;
        /** @brief Multiplier applied to the float4 input before rounding to an index. */
        float scale = 1.0f;
    };

    /**
     * @brief The bone-index input a BLENDINDICES element of a given format feeds.
     *
     * plans/plan_apple_m4.md AM4-152: Direct3D 9 hands every vertex input to the shader as float,
     * so XNA draws BLENDINDICES in any format -- CustomModelAnimation's SkinnedModelProcessor
     * writes Vector4. Byte4 stays the uchar4 input; a float, half or normalized format feeds the
     * float4 input as its value; Short2/Short4, which Metal will not convert to float, are fetched
     * normalized and scaled back by 32767 (exact for every non-negative index).
     *
     * @param format Element format.
     * @return The fetch kind and the input it feeds.
     */
    [[nodiscard]] inline MetalBoneIndexInput MetalBoneIndexInputFor(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format) noexcept
    {
        using VEF = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        switch (format)
        {
            case VEF::Byte4:  return {MetalVertexAttribKind::UChar4, false, 1.0f};
            case VEF::Short2: return {MetalVertexAttribKind::Short2Normalized, true, 32767.0f};
            case VEF::Short4: return {MetalVertexAttribKind::Short4Normalized, true, 32767.0f};
            default:          return {DescribeMetalVertexElementFormat(format).kind, true, 1.0f};
        }
    }

    /**
     * @brief Whether an element format can feed a semantic's shader type.
     *
     * Metal converts a float, half or normalized format to the shader's float vector, but not a
     * raw integer format. BLENDINDICES takes any format, as Direct3D 9 does: Byte4 feeds the
     * skinning shaders' `uchar4` input, every other format their float4 one (a pipeline variant,
     * plans/plan_apple_m4.md AM4-152; MetalBoneIndexInputFor).
     *
     * @param semantic Shader input.
     * @param format Element format.
     * @return True when Metal can bind the format to that input.
     */
    [[nodiscard]] constexpr bool MetalSemanticAcceptsFormat(
        MetalVertexSemantic semantic,
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format) noexcept
    {
        (void)semantic;
        (void)format;
        return true;   // AM4-155: every format reaches every input (MetalIntegerElementScale)
    }

    /**
     * @brief Whether a format is a raw (unnormalized) integer one Metal will not fetch as float.
     *
     * @param format Element format.
     * @return True for Byte4, Short2 and Short4.
     */
    [[nodiscard]] constexpr bool MetalIsRawIntegerFormat(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format) noexcept
    {
        using VEF = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        return format == VEF::Byte4 || format == VEF::Short2 || format == VEF::Short4;
    }

    /**
     * @brief The function-constant slot of a float semantic's scale; -1 for BLENDINDICES.
     *
     * @param semantic Shader input.
     * @return 0..6 for POSITION, NORMAL, TEXCOORD0, TEXCOORD1, COLOR, TANGENT and BLENDWEIGHT
     *         (function constants 4..10 of the stock shaders).
     */
    [[nodiscard]] constexpr int MetalSemanticScaleSlot(MetalVertexSemantic semantic) noexcept
    {
        switch (semantic)
        {
            case MetalVertexSemantic::Position:    return 0;
            case MetalVertexSemantic::Normal:      return 1;
            case MetalVertexSemantic::TexCoord0:   return 2;
            case MetalVertexSemantic::TexCoord1:   return 3;
            case MetalVertexSemantic::Color:       return 4;
            case MetalVertexSemantic::Tangent:     return 5;
            case MetalVertexSemantic::BlendWeight: return 6;
            case MetalVertexSemantic::BlendIndices: return -1;
        }
        return -1;
    }

    /** @brief How a raw integer element reaches a float input (AM4-155). */
    struct MetalIntegerElementInput
    {
        /** @brief The normalized vertex format Metal fetches it as. */
        MetalVertexAttribKind kind = MetalVertexAttribKind::UChar4Normalized;
        /** @brief The per-component factor that undoes the normalization (1 past its components). */
        std::array<float, 4> scale{1.0f, 1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief The fetch and scale of a raw integer element feeding a float input.
     *
     * plans/plan_apple_m4.md AM4-155: Direct3D 9 converts UBYTE4/SHORT2/SHORT4 to float; Metal
     * fetches them only into integer shader types. The element is fetched normalized and multiplied
     * back component by component -- only the components the format supplies, so a Short2 colour's
     * alpha stays the fetch's 1 -- and rounded, which recovers the integer exactly.
     *
     * @param format A raw integer element format (MetalIsRawIntegerFormat).
     * @return The normalized fetch kind and its scale.
     */
    [[nodiscard]] inline MetalIntegerElementInput MetalIntegerElementScale(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format) noexcept
    {
        using VEF = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        switch (format)
        {
            case VEF::Byte4:
                return {MetalVertexAttribKind::UChar4Normalized, {255.0f, 255.0f, 255.0f, 255.0f}};
            case VEF::Short2:
                return {MetalVertexAttribKind::Short2Normalized, {32767.0f, 32767.0f, 1.0f, 1.0f}};
            case VEF::Short4:
                return {MetalVertexAttribKind::Short4Normalized, {32767.0f, 32767.0f, 32767.0f, 32767.0f}};
            default:
                return {DescribeMetalVertexElementFormat(format).kind, {1.0f, 1.0f, 1.0f, 1.0f}};
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
     * plans/plan_apple_m4.md AM4-080: Metal's vertex functions are coarser than XNA's stock-effect
     * permutations -- one lit function serves the textured and untextured BasicEffect alike -- so
     * a function may read a semantic the active permutation does not. XNA accepts a declaration
     * without that semantic (an untextured BasicEffect over VertexPositionNormal); such an input
     * is read from a constant instead: zero for texture coordinates, normals and tangents, opaque
     * white for colour. A semantic the permutation does use still refuses, as XNA does
     * ("TextureCoordinate0 is missing").
     *
     * @param kind Pipeline kind the draw selected.
     * @param elements The declaration's elements.
     * @param recordStride Bytes per record in the bound buffer.
     * @param params The draw's effect parameters; null keeps every semantic required.
     * @return The complete input, or one carrying the refusal reason.
     */
    [[nodiscard]] inline MetalDeclaredVertexInput BuildMetalDeclaredVertexInput(
        MetalPipelineKind kind,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        int recordStride,
        const CNA::Internal::Renderers::GpuDrawParams* params = nullptr)
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
            // plans/plan_apple_m4.md AM4-084: glTF 2.0 3.9.2 makes COLOR_0 a linear multiplier on PBR
            // base colour. The record's colour is read only when the effect's VertexColorEnabledEXT
            // asks for it; otherwise -- and for a record without one, glTF's absent COLOR_0 -- the
            // multiplier is the identity, the constant block's opaque white.
            // AM4-098: the lit BasicEffect functions multiply by COLOR0 unconditionally (AM4-081), and
            // XNA's BasicEffect reads it only in its VertexColorEnabled permutations, so the same
            // switch decides there: a declared colour with the switch off is ignored, as on XNA.
            const bool pbrKind = kind == MetalPipelineKind::Pbr48 || kind == MetalPipelineKind::SkinnedPbr68;
            const bool litKind = (kind == MetalPipelineKind::LitTex32 || kind == MetalPipelineKind::LitTex32VertexLit) &&
                                 params != nullptr;
            if (semantic == MetalVertexSemantic::Color && (pbrKind || litKind) &&
                !(params != nullptr && params->vertexColorEnabled && element != nullptr))
            {
                input.attributes.push_back(MetalDeclaredAttribute{
                    static_cast<int>(location), MetalVertexAttribKind::Float4,
                    kMetalConstantAttributeOneOffset, true});
                continue;
            }
            if (!element && params != nullptr)
            {
                Microsoft::Xna::Framework::Graphics::VertexElementUsage usage{};
                int usageIndex = 0;
                MetalSemanticUsage(semantic, usage, usageIndex);
                if (!CNA::Internal::Renderers::StockEffectUsesVertexSemantic(*params, usage, usageIndex))
                {
                    const bool colour = semantic == MetalVertexSemantic::Color;
                    const MetalVertexAttribKind constantKind =
                        colour || semantic == MetalVertexSemantic::Tangent ? MetalVertexAttribKind::Float4
                        : semantic == MetalVertexSemantic::Normal          ? MetalVertexAttribKind::Float3
                                                                           : MetalVertexAttribKind::Float2;
                    input.attributes.push_back(MetalDeclaredAttribute{
                        static_cast<int>(location), constantKind,
                        colour ? kMetalConstantAttributeOneOffset : kMetalConstantAttributeZeroOffset,
                        true});
                    continue;
                }
            }
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
            MetalVertexAttribKind kind = DescribeMetalVertexElementFormat(format).kind;
            if (semantic == MetalVertexSemantic::BlendIndices)
            {
                const MetalBoneIndexInput bones = MetalBoneIndexInputFor(format);
                kind = bones.kind;
                input.boneIndicesFloat = bones.floatInput;
                input.boneIndexScale = bones.scale;
            }
            else if (MetalIsRawIntegerFormat(format))
            {
                const MetalIntegerElementInput integer = MetalIntegerElementScale(format);
                kind = integer.kind;
                input.semanticScales[static_cast<std::size_t>(MetalSemanticScaleSlot(semantic))] = integer.scale;
            }
            input.attributes.push_back(MetalDeclaredAttribute{static_cast<int>(location), kind, offset});
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
     * @brief The canonical elements for an undeclared buffer of a given stride.
     *
     * plans/plan_apple_m4.md AM4-084: the PBR kinds each serve more than one record. Stride 60 is
     * the stride-48 record with TEXCOORD_1 and a packed COLOR_0 appended (GLTF-182/462), stride 76
     * the stride-68 record with TEXCOORD_1 (GLTF-386), and stride 80 that with COLOR_0 as well
     * (GLTF-463) -- the layouts every other renderer binds for those strides.
     *
     * @param kind Pipeline kind the draw selected.
     * @param stride The undeclared buffer's stride.
     * @return The canonical elements; empty for Sprite2D.
     */
    [[nodiscard]] inline std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>
    MetalCanonicalElementsFor(MetalPipelineKind kind, std::size_t stride)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using F = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using U = Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        auto elements = MetalCanonicalElementsFor(kind);
        if (kind != MetalPipelineKind::Pbr48 && kind != MetalPipelineKind::SkinnedPbr68)
            return elements;
        switch (stride)
        {
            case 60:
                elements.push_back(VertexElement(48, F::Vector2, U::TextureCoordinate, 1));
                elements.push_back(VertexElement(56, F::Color, U::Color, 0));
                break;
            case 76:
                elements.push_back(VertexElement(68, F::Vector2, U::TextureCoordinate, 1));
                break;
            case 80:
                elements.push_back(VertexElement(68, F::Vector2, U::TextureCoordinate, 1));
                elements.push_back(VertexElement(76, F::Color, U::Color, 0));
                break;
            default:
                break;
        }
        return elements;
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
     * A BasicEffect with VertexColorEnabled over a vertex that carries COLOR0 multiplies by it on
     * XNA. A pipeline whose vertex function reads no colour would silently drop it, so such a draw
     * is refused instead. Every BasicEffect-family function reads COLOR0 since AM4-081, so this is
     * a guard for future pipeline kinds. Only the BasicEffect family is judged: the other stock
     * effects either read colour themselves or have no VertexColorEnabled.
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

    /** @brief Each public stream's declaration elements, by index into `GpuDrawParams::vertexStreams`. */
    using MetalStreamDeclarations =
        std::array<const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>*,
                   CNA::Internal::Renderers::kMaxVertexStreams>;

    namespace MetalDeclaredVertexInputDetail
    {
        [[nodiscard]] constexpr int AttribByteSize(MetalVertexAttribKind kind) noexcept
        {
            switch (kind)
            {
                case MetalVertexAttribKind::Float1:           return 4;
                case MetalVertexAttribKind::Float2:           return 8;
                case MetalVertexAttribKind::Float3:           return 12;
                case MetalVertexAttribKind::Float4:           return 16;
                case MetalVertexAttribKind::UChar4Normalized: return 4;
                case MetalVertexAttribKind::UChar4:           return 4;
                case MetalVertexAttribKind::Short2:           return 4;
                case MetalVertexAttribKind::Short4:           return 8;
                case MetalVertexAttribKind::Short2Normalized: return 4;
                case MetalVertexAttribKind::Short4Normalized: return 8;
                case MetalVertexAttribKind::Half2:            return 4;
                case MetalVertexAttribKind::Half4:            return 8;
            }
            return 16;
        }
    }

    /**
     * @brief The one declaration a draw's per-vertex streams make together.
     *
     * plans/plan_apple_m4.md AM4-143: each per-vertex stream's elements, moved to its place in the
     * combined vertex (`combinedByteBase`) and carrying its binding-time usage index (XNA's
     * collision remap, SOFTWARE-320), in public slot order -- what the stock pipeline selection and
     * semantic lookup read, as they read a single stream's declaration.
     *
     * @param params The draw.
     * @param declarations Each stream's declaration elements; null for a stream without one.
     * @return The combined elements.
     */
    [[nodiscard]] inline std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>
    MetalCombinedPerVertexElements(const CNA::Internal::Renderers::GpuDrawParams& params,
                                   const MetalStreamDeclarations& declarations)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        std::vector<VertexElement> combined;
        for (int i = 0; i < params.vertexStreamCount; ++i)
        {
            const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
            const auto* elements = declarations[static_cast<std::size_t>(i)];
            if (stream.instanceFrequency != 0 || elements == nullptr)
                continue;
            for (std::size_t j = 0; j < elements->size(); ++j)
            {
                const VertexElement& e = (*elements)[j];
                combined.emplace_back(stream.combinedByteBase + e.getOffsetProperty(),
                                      e.getVertexElementFormatProperty(),
                                      e.getVertexElementUsageProperty(),
                                      stream.EffectiveUsageIndex(j, e.getUsageIndexProperty()));
            }
        }
        return combined;
    }

    /**
     * @brief The vertex input of a draw with several streams, per-vertex or per-instance.
     *
     * plans/plan_apple_m4.md AM4-143. The per-vertex streams feed the stock function's semantics
     * exactly as one stream would: the lookup runs on their combined declaration
     * (MetalCombinedPerVertexElements), and each attribute found is then bound to the stream that
     * holds it, at its offset inside that stream. Only a stream that supplies an attribute gets a
     * layout, as XNA fetches only what the shader declares.
     *
     * The per-instance streams supply the stock effects' per-instance world matrix, CNA's
     * instancing contract on every renderer (EasyGL's REMED-GFX-122): their declarations,
     * concatenated in slot order, give the matrix its columns from their first four elements,
     * whatever their usages -- EasyGL's attribute locations 12..15. A column no element supplies
     * reads (0, 0, 0, 1), as a GL location without an array does. Each such stream steps once per
     * `InstanceFrequency` instances.
     *
     * @param kind Pipeline kind the draw selected.
     * @param params The draw; its stream list must have passed DescribeMetalDrawStreamPolicy.
     * @param declarations Each stream's declaration elements, by stream index.
     * @return The complete input, or one carrying the refusal reason.
     */
    [[nodiscard]] inline MetalDeclaredVertexInput BuildMetalStreamVertexInput(
        MetalPipelineKind kind,
        const CNA::Internal::Renderers::GpuDrawParams& params,
        const MetalStreamDeclarations& declarations)
    {
        using namespace MetalDeclaredVertexInputDetail;
        using CNA::Internal::Renderers::kMaxVertexStreams;
        MetalDeclaredVertexInput input = BuildMetalDeclaredVertexInput(
            kind, MetalCombinedPerVertexElements(params, declarations), params.combinedVertexStride,
            &params);
        if (!input.IsComplete())
            return input;

        std::array<bool, kMaxVertexStreams> used{};
        for (MetalDeclaredAttribute& attribute : input.attributes)
        {
            if (attribute.constant)
                continue;
            const auto slot = CNA::Internal::Renderers::MapCombinedOffsetToStream(params, attribute.offset);
            const auto& stream = params.vertexStreams[static_cast<std::size_t>(slot.streamIndex)];
            if (slot.byteOffsetInStream + AttribByteSize(attribute.kind) > stream.strideInBytes)
            {
                input.refusal = "an element of vertex stream " + std::to_string(stream.slot) +
                                " reaches past that stream's " + std::to_string(stream.strideInBytes) +
                                "-byte record";
                return input;
            }
            attribute.offset = slot.byteOffsetInStream;
            attribute.bufferIndex = MetalVertexStreamBufferIndex(stream.slot);
            used[static_cast<std::size_t>(slot.streamIndex)] = true;
        }

        int column = 0;
        for (int i = 0; i < params.vertexStreamCount && column < 4; ++i)
        {
            const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
            const auto* elements = declarations[static_cast<std::size_t>(i)];
            if (stream.instanceFrequency <= 0)
                continue;
            if (elements == nullptr || elements->empty())
            {
                input.refusal = "per-instance vertex stream " + std::to_string(stream.slot) +
                                " has no VertexDeclaration";
                return input;
            }
            for (std::size_t j = 0; j < elements->size() && column < 4; ++j, ++column)
            {
                const auto& e = (*elements)[j];
                const auto format = e.getVertexElementFormatProperty();
                if (MetalIsRawIntegerFormat(format))
                {
                    input.refusal = "per-instance vertex stream " + std::to_string(stream.slot) +
                                    " has an integer element Metal cannot convert to the instance "
                                    "matrix's float4 column";
                    return input;
                }
                const int offset = e.getOffsetProperty();
                if (offset < 0 || offset + MetalVertexElementByteSize(format) > stream.strideInBytes)
                {
                    input.refusal = "an element of per-instance vertex stream " +
                                    std::to_string(stream.slot) + " lies outside its " +
                                    std::to_string(stream.strideInBytes) + "-byte record";
                    return input;
                }
                input.attributes.push_back(MetalDeclaredAttribute{
                    kMetalInstanceMatrixLocation + column, DescribeMetalVertexElementFormat(format).kind,
                    offset, false, MetalVertexStreamBufferIndex(stream.slot)});
                used[static_cast<std::size_t>(i)] = true;
            }
        }
        if (CNA::Internal::Renderers::InstanceStreamCount(params) > 0)
        {
            input.instanceMatrix = true;
            for (; column < 4; ++column)
                input.attributes.push_back(MetalDeclaredAttribute{
                    kMetalInstanceMatrixLocation + column, MetalVertexAttribKind::Float4,
                    kMetalConstantAttributeUnitWOffset, true});
        }

        for (int i = 0; i < params.vertexStreamCount; ++i)
        {
            if (!used[static_cast<std::size_t>(i)])
                continue;
            const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
            input.layouts.push_back(MetalDeclaredLayout{
                MetalVertexStreamBufferIndex(stream.slot), stream.strideInBytes,
                stream.instanceFrequency > 0 ? stream.instanceFrequency : 0});
        }
        return input;
    }
}
