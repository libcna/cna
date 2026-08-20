// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

/**
 * @file
 * @brief REMED-GFX-DECL-GUARD -- the declaration-fidelity safety boundary.
 *
 * REMED-GFX-217 records that seven rasterizing renderers (Vulkan, Software, WebGPU, SDL_GPU, D3D9,
 * D3D11, D3D12) do not translate a `VertexDeclaration` at all: they select a native input layout
 * from a canonical byte-stride table and discard everything else the declaration states. A stride
 * does not determine element composition, so two declarations that share a stride share a native
 * layout, and the one that does not match it is read from the wrong bytes -- accepted, submitted,
 * and silently wrong.
 *
 * This file is NOT a translator. Complete per-renderer declaration translation stays in
 * `plans/plan_postaudit.md`. What it provides is the boundary that makes the gap safe until then: a pure
 * predicate that decides whether a declaration can be represented faithfully by the layout a
 * renderer actually programs, so an unrepresentable one fails deterministically before any native
 * layout, pipeline, command or submission exists instead of rendering the wrong thing.
 *
 * ## The rule is asymmetric, and deliberately so
 *
 * "The declaration must equal the table entry" is wrong and was rejected by the exit triage: a
 * position-only stride-12 declaration renders correctly on Vulkan today (its fallback layout also
 * binds a colour attribute at offset 12, but a declaration that names no colour supplies no colour
 * for a stock program to consume), and an equality rule would reject it. Only what the declaration
 * ACTUALLY DECLARES has to survive:
 *
 *  - R0 the native record advance equals the declared stride;
 *  - R1 no native fetch reads bytes a declared element owns under any other semantic, usage index,
 *       offset or format -- this is the reinterpretation the whole guard exists to stop;
 *  - R2 every declared element has a native attribute with the same usage AND usage index, at the
 *       same byte offset, in the same format (which carries component type, count and
 *       normalization);
 *  - R3 every declared element lies wholly inside the declared stride;
 *  - R4 declared elements do not overlap one another.
 *
 * A native attribute the declaration does not name is NOT a violation: nothing declared is being
 * reinterpreted, and rejecting it would remove layouts that are correct today.
 *
 * ## Why this is header-only
 *
 * Each renderer is its own static library, linked against `cna_renderer_common` and
 * SharpRuntime rather than against the CNA library, so a translation unit under `src/CNA/Internal/
 * Graphics/` is not visible to them. Inline definitions keep the guard available to all seven
 * renderers without adding a shared build target, and the whole predicate is a few hundred bytes of
 * branch-free comparison per draw.
 */
namespace CNA::Internal::Graphics
{
    /**
     * @brief One attribute of the native input layout a renderer actually programs.
     *
     * This is the renderer's own truth, not the public declaration: the semantic, usage index, byte
     * offset and format the native vertex fetch uses.
     */
    struct InferredVertexElement
    {
        /** @brief The semantic the native fetch supplies. */
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage{};
        /** @brief The semantic's usage index. */
        int usageIndex = 0;
        /** @brief The byte offset the native fetch reads from. */
        int offset = 0;
        /** @brief The format the native fetch reads, including its normalization. */
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format{};
    };

    /**
     * @brief The native input layout a renderer infers for one byte stride.
     *
     * @c known is false when the renderer has no inferred layout for that stride at all. The guard
     * then abstains: the renderer's own out-of-table rejection is already loud and deterministic,
     * and replacing it would change an established boundary for no safety gain.
     */
    struct InferredVertexLayout
    {
        /** @brief The attributes, or null when @c known is false. */
        const InferredVertexElement* elements = nullptr;
        /** @brief How many attributes @c elements holds. */
        std::size_t count = 0;
        /** @brief Whether this renderer has an inferred layout for the stride at all. */
        bool known = false;
    };

    /**
     * @brief What a renderer's route does with a stride its canonical table does not list.
     *
     * Each value is a measured property of one renderer's route, not a preference.
     */
    enum class UnlistedStrideLayout
    {
        /**
         * @brief The renderer refuses the stride itself, before any native work.
         *
         * Software, WebGPU's ordinary route and D3D9/D3D11/D3D12 all throw on an out-of-table
         * stride today. The guard abstains and leaves that rejection exactly as it is.
         */
        RendererRefusesIt,
        /**
         * @brief The renderer falls back to its `VertexPositionColor` layout: Position at offset 0
         * and a packed colour at offset 12, strided by the buffer's own stride.
         *
         * Vulkan's and SDL_GPU's ordinary routes do this. It is why a position-only stride-12
         * declaration renders correctly on Vulkan today.
         */
        PositionColorFallback,
        /**
         * @brief The renderer falls back to a position-only layout: Position at offset 0 and
         * nothing else.
         *
         * The instanced modules of Vulkan and WebGPU do this for every stride their packed-colour
         * table does not list.
         */
        PositionOnlyFallback
    };

    /**
     * @brief The declaration a stride-inferring renderer must still remember.
     *
     * These renderers left `IVertexBufferRenderer::SetVertexDeclaration` empty, so the declaration
     * that reached them was thrown away and nothing downstream could compare it against anything.
     * Storing it costs one vector per buffer and is what makes the guard possible without widening
     * any interface.
     */
    class DeclaredVertexLayout
    {
    public:
        /**
         * @brief Stores @p declaration's elements and stride.
         *
         * @param declaration The declaration the caller propagated for this buffer.
         */
        void Remember(const Microsoft::Xna::Framework::Graphics::VertexDeclaration& declaration)
        {
            elements_ = declaration.GetVertexElements();
            stride_ = declaration.getVertexStrideProperty();
        }

        /**
         * @brief The stored elements, empty when no declaration was ever propagated.
         *
         * @return The declaration's element list.
         */
        [[nodiscard]] const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>&
        GetElements() const noexcept { return elements_; }

        /**
         * @brief The stored stride, zero when no declaration was ever propagated.
         *
         * @return The declaration's byte stride.
         */
        [[nodiscard]] int GetStride() const noexcept { return stride_; }

        /**
         * @brief Whether no declaration has been propagated yet.
         *
         * @return True when there is nothing to compare against.
         */
        [[nodiscard]] bool IsEmpty() const noexcept { return elements_.empty(); }

    private:
        std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> elements_;
        int stride_ = 0;
    };

    /**
     * @brief One input a stock shader program declares, at its own attribute location.
     *
     * REMED-GFX-218's mechanism is not the stride table: EasyGL receives the declaration and binds
     * each element at the location matching its INDEX in the element list, while its stock programs
     * assign a different meaning to the same location depending on which program was selected --
     * location 1 is `aColor` at stride 16, `aUV` at 20 and `aNormal` at 32. There is therefore no
     * global semantic-to-location function, and the only truthful comparison is against the
     * selected program's own ordered input list.
     */
    struct StockProgramInput
    {
        /** @brief The semantic this location supplies. */
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage{};
        /** @brief The semantic's usage index. */
        int usageIndex = 0;
        /** @brief The format the shader input expects. */
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format{};
        /** @brief The attribute's name in the shader, for the diagnostic. */
        const char* name = "";
    };

    /// Implementation detail of the predicate below; not part of any renderer's contract.
    namespace VertexDeclarationFidelityDetail
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        inline int FormatSize(VertexElementFormat format) noexcept
        {
            switch (format)
            {
                case VertexElementFormat::Single:           return 4;
                case VertexElementFormat::Vector2:          return 8;
                case VertexElementFormat::Vector3:          return 12;
                case VertexElementFormat::Vector4:          return 16;
                case VertexElementFormat::Color:            return 4;
                case VertexElementFormat::Byte4:            return 4;
                case VertexElementFormat::Short2:           return 4;
                case VertexElementFormat::Short4:           return 8;
                case VertexElementFormat::NormalizedShort2: return 4;
                case VertexElementFormat::NormalizedShort4: return 8;
                case VertexElementFormat::HalfVector2:      return 4;
                case VertexElementFormat::HalfVector4:      return 8;
            }
            return 0;
        }

        inline const char* UsageName(VertexElementUsage usage) noexcept
        {
            switch (usage)
            {
                case VertexElementUsage::Position:          return "Position";
                case VertexElementUsage::Color:             return "Color";
                case VertexElementUsage::TextureCoordinate: return "TextureCoordinate";
                case VertexElementUsage::Normal:            return "Normal";
                case VertexElementUsage::Binormal:          return "Binormal";
                case VertexElementUsage::Tangent:           return "Tangent";
                case VertexElementUsage::BlendIndices:      return "BlendIndices";
                case VertexElementUsage::BlendWeight:       return "BlendWeight";
                case VertexElementUsage::Depth:             return "Depth";
                case VertexElementUsage::Fog:               return "Fog";
                case VertexElementUsage::PointSize:         return "PointSize";
                case VertexElementUsage::Sample:            return "Sample";
                case VertexElementUsage::TessellateFactor:  return "TessellateFactor";
            }
            return "?";
        }

        inline const char* FormatName(VertexElementFormat format) noexcept
        {
            switch (format)
            {
                case VertexElementFormat::Single:           return "Single";
                case VertexElementFormat::Vector2:          return "Vector2";
                case VertexElementFormat::Vector3:          return "Vector3";
                case VertexElementFormat::Vector4:          return "Vector4";
                case VertexElementFormat::Color:            return "Color";
                case VertexElementFormat::Byte4:            return "Byte4";
                case VertexElementFormat::Short2:           return "Short2";
                case VertexElementFormat::Short4:           return "Short4";
                case VertexElementFormat::NormalizedShort2: return "NormalizedShort2";
                case VertexElementFormat::NormalizedShort4: return "NormalizedShort4";
                case VertexElementFormat::HalfVector2:      return "HalfVector2";
                case VertexElementFormat::HalfVector4:      return "HalfVector4";
            }
            return "?";
        }

        inline std::string Describe(VertexElementUsage usage, int usageIndex, int offset,
                                    VertexElementFormat format)
        {
            return std::string(UsageName(usage)) + std::to_string(usageIndex) + '@' +
                   std::to_string(offset) + ' ' + FormatName(format);
        }

        inline bool RangesOverlap(int aStart, int aSize, int bStart, int bSize) noexcept
        {
            return aStart < bStart + bSize && bStart < aStart + aSize;
        }

        // THE CANONICAL STRIDE TABLE. Every renderer in REMED-GFX-217's scope transcribes exactly
        // this: D3DVertexFormatHelper's kStride16..kStride68 arrays, EasyGLRenderer's
        // ApplyLayout switch, VulkanRenderer's per-pipeline attribute arrays and
        // SoftwareRenderer's BuildGenericClipVertex byte reader. All four agree because all
        // four were derived from the same seven built-in XNA vertex types, whose declarations take
        // their offsets from CNA/Internal/Graphics/BuiltInVertexStreams.hpp.

        inline constexpr InferredVertexElement kStride16[] = {
            {VertexElementUsage::Position, 0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Color,    0, 12, VertexElementFormat::Color},
        };

        inline constexpr InferredVertexElement kStride20[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::TextureCoordinate, 0, 12, VertexElementFormat::Vector2},
        };

        inline constexpr InferredVertexElement kStride24[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Color,             0, 12, VertexElementFormat::Color},
            {VertexElementUsage::TextureCoordinate, 0, 16, VertexElementFormat::Vector2},
        };

        inline constexpr InferredVertexElement kStride32[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::TextureCoordinate, 0, 24, VertexElementFormat::Vector2},
        };

        inline constexpr InferredVertexElement kStride48[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::Tangent,           0, 24, VertexElementFormat::Vector4},
            {VertexElementUsage::TextureCoordinate, 0, 40, VertexElementFormat::Vector2},
        };

        inline constexpr InferredVertexElement kStride52[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::TextureCoordinate, 0, 24, VertexElementFormat::Vector2},
            {VertexElementUsage::BlendWeight,       0, 32, VertexElementFormat::Vector4},
            {VertexElementUsage::BlendIndices,      0, 48, VertexElementFormat::Byte4},
        };

        inline constexpr InferredVertexElement kStride56[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::TextureCoordinate, 0, 24, VertexElementFormat::Vector2},
            {VertexElementUsage::BlendWeight,       0, 32, VertexElementFormat::Vector4},
            {VertexElementUsage::BlendIndices,      0, 48, VertexElementFormat::Byte4},
            {VertexElementUsage::Color,             0, 52, VertexElementFormat::Color},
        };

        // GLTF-182 deliberately padded the naturally 56-byte rigid PBR+UV1 record to 60 bytes:
        // stride 56 already denotes skinned+colour, and keeping one meaning per stride avoids an
        // effect-dependent interpretation of the same vertex declaration.
        //
        // GLTF-462 gives those four bytes a job rather than leaving them reserved -- they are the
        // packed COLOR_0 of a vertex-coloured metallic-roughness primitive, which §3.7.2.1 makes an
        // additional linear multiplier on base colour. Offsets 0..55 are unchanged, so every
        // renderer that already binds this stride is unaffected, and a primitive with no COLOR_0
        // writes opaque white -- the identity multiplier -- so reading the slot can never darken a
        // draw that used to be right. `GpuDrawParams::vertexColorEnabled` says which it is.
        inline constexpr InferredVertexElement kStride60[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::Tangent,           0, 24, VertexElementFormat::Vector4},
            {VertexElementUsage::TextureCoordinate, 0, 40, VertexElementFormat::Vector2},
            {VertexElementUsage::TextureCoordinate, 1, 48, VertexElementFormat::Vector2},
            {VertexElementUsage::Color,             0, 56, VertexElementFormat::Color},
        };

        inline constexpr InferredVertexElement kStride68[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::Tangent,           0, 24, VertexElementFormat::Vector4},
            {VertexElementUsage::TextureCoordinate, 0, 40, VertexElementFormat::Vector2},
            {VertexElementUsage::BlendWeight,       0, 48, VertexElementFormat::Vector4},
            {VertexElementUsage::BlendIndices,      0, 64, VertexElementFormat::Byte4},
        };

        inline constexpr InferredVertexElement kStride76[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::Tangent,           0, 24, VertexElementFormat::Vector4},
            {VertexElementUsage::TextureCoordinate, 0, 40, VertexElementFormat::Vector2},
            {VertexElementUsage::BlendWeight,       0, 48, VertexElementFormat::Vector4},
            {VertexElementUsage::BlendIndices,      0, 64, VertexElementFormat::Byte4},
            {VertexElementUsage::TextureCoordinate, 1, 68, VertexElementFormat::Vector2},
        };

        // GLTF-463: the skinned counterpart of stride 60's colour slot. The skinned PBR record has no
        // reserved bytes to reuse -- stride 76 is exactly its seven fields -- so a skinned,
        // vertex-coloured metallic-roughness primitive gets its own stride, with the whole stride-76
        // record as a byte-for-byte prefix and the colour appended.
        inline constexpr InferredVertexElement kStride80[] = {
            {VertexElementUsage::Position,          0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Normal,            0, 12, VertexElementFormat::Vector3},
            {VertexElementUsage::Tangent,           0, 24, VertexElementFormat::Vector4},
            {VertexElementUsage::TextureCoordinate, 0, 40, VertexElementFormat::Vector2},
            {VertexElementUsage::BlendWeight,       0, 48, VertexElementFormat::Vector4},
            {VertexElementUsage::BlendIndices,      0, 64, VertexElementFormat::Byte4},
            {VertexElementUsage::TextureCoordinate, 1, 68, VertexElementFormat::Vector2},
            {VertexElementUsage::Color,             0, 76, VertexElementFormat::Color},
        };


        // The two fallbacks a renderer uses for a stride the table above does not list. Both are
        // measured behaviours, not guesses: Vulkan's ordinary route renders a position-only
        // stride-12 buffer correctly through the first, and WebGPU's instanced module renders the
        // same buffer correctly through the second.
        inline constexpr InferredVertexElement kPositionColorFallback[] = {
            {VertexElementUsage::Position, 0,  0, VertexElementFormat::Vector3},
            {VertexElementUsage::Color,    0, 12, VertexElementFormat::Color},
        };

        inline constexpr InferredVertexElement kPositionOnlyFallback[] = {
            {VertexElementUsage::Position, 0, 0, VertexElementFormat::Vector3},
        };

        template <std::size_t N>
        constexpr InferredVertexLayout Layout(const InferredVertexElement (&elements)[N]) noexcept
        {
            return InferredVertexLayout{elements, N, true};
        }
    }

    /**
     * @brief The canonical native layout for @p strideInBytes, or @p fallback's shape.
     *
     * @param strideInBytes The record stride the renderer strides the buffer by.
     * @param fallback What this renderer's route does with a stride the table does not list.
     * @return The inferred layout; `known == false` when the renderer refuses the stride itself.
     */
    [[nodiscard]] inline InferredVertexLayout InferredLayoutForStride(
        int strideInBytes, UnlistedStrideLayout fallback) noexcept
    {
        namespace detail = VertexDeclarationFidelityDetail;
        switch (strideInBytes)
        {
            case 16: return detail::Layout(detail::kStride16);
            case 20: return detail::Layout(detail::kStride20);
            case 24: return detail::Layout(detail::kStride24);
            case 32: return detail::Layout(detail::kStride32);
            case 48: return detail::Layout(detail::kStride48);
            case 52: return detail::Layout(detail::kStride52);
            case 56: return detail::Layout(detail::kStride56);
            case 60: return detail::Layout(detail::kStride60);
            case 68: return detail::Layout(detail::kStride68);
            case 76: return detail::Layout(detail::kStride76);
            case 80: return detail::Layout(detail::kStride80);
            default: break;
        }
        switch (fallback)
        {
            case UnlistedStrideLayout::PositionColorFallback:
                return detail::Layout(detail::kPositionColorFallback);
            case UnlistedStrideLayout::PositionOnlyFallback:
                return detail::Layout(detail::kPositionOnlyFallback);
            case UnlistedStrideLayout::RendererRefusesIt:
                break;
        }
        return InferredVertexLayout{};
    }

    /**
     * @brief Where the canonical layout for one stride puts one semantic.
     *
     * plans/plan_gltf.md `GLTF-473`. `InferredLayoutForStride` answers "what does this stride mean";
     * this answers the narrower question a **fixed-function** renderer has to ask before it binds a
     * client array: *at which byte offset does this stride's canonical record carry Normal / Color /
     * TextureCoordinate 0?* A renderer that hard-codes that offset instead of asking is reading one
     * layout's bytes through another's rule, and the two agree only by coincidence.
     *
     * The two "not found" cases are kept apart on purpose, because a caller must treat them
     * differently: a stride the table does not list is a layout this file has no opinion about (a
     * renderer-local record), and abstaining is right; a stride it does list which simply has no such
     * semantic is a definite answer, and binding an array for it is a defect.
     *
     * @param strideInBytes The record stride the renderer strides the buffer by.
     * @param usage The semantic being looked for.
     * @param usageIndex The semantic's usage index.
     * @return The offset, with `strideKnown` false for an unlisted stride and `present` false when
     *         the listed layout carries no such element.
     */
    struct CanonicalSemanticOffsetEXT
    {
        /** @brief False when the canonical table lists no layout for the stride at all. */
        bool strideKnown = false;
        /** @brief True when the listed layout carries the requested semantic. */
        bool present = false;
        /** @brief The byte offset, meaningful only when @c present. */
        int offset = 0;
    };

    /**
     * @brief Looks up @p usage / @p usageIndex in the canonical layout for @p strideInBytes.
     *
     * @param strideInBytes The record stride.
     * @param usage The semantic being looked for.
     * @param usageIndex The semantic's usage index.
     * @return Where the semantic lives, or why it could not be answered.
     */
    [[nodiscard]] inline CanonicalSemanticOffsetEXT CanonicalOffsetOfSemanticEXT(
        int strideInBytes,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
        int usageIndex) noexcept
    {
        const InferredVertexLayout layout =
            InferredLayoutForStride(strideInBytes, UnlistedStrideLayout::RendererRefusesIt);
        if (!layout.known) { return CanonicalSemanticOffsetEXT{false, false, 0}; }
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == usageIndex)
            {
                return CanonicalSemanticOffsetEXT{true, true, layout.elements[i].offset};
            }
        }
        return CanonicalSemanticOffsetEXT{true, false, 0};
    }

    /**
     * @brief Why @p declaredElements cannot be represented by @p inferred, or an empty string.
     *
     * Pure: it allocates a message only when it has one to give, touches no device state and never
     * throws.
     *
     * @param declaredElements The public declaration's elements.
     * @param declaredStride The public declaration's byte stride.
     * @param nativeRecordStride The stride the renderer actually advances records by.
     * @param inferred The native layout the renderer actually programs.
     * @return An empty string when the declaration is faithfully representable, otherwise a
     *         diagnostic naming the element and the incompatibility.
     */
    [[nodiscard]] inline std::string DescribeUnrepresentableVertexDeclaration(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
        int declaredStride,
        int nativeRecordStride,
        const InferredVertexLayout& inferred)
    {
        namespace detail = VertexDeclarationFidelityDetail;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        // Nothing was declared, so nothing can be reinterpreted. Buffers whose declaration was
        // never propagated keep exactly the behaviour they have today.
        if (declaredElements.empty()) return {};
        // No upload has happened yet, so there is no record advance to compare against.
        if (nativeRecordStride <= 0) return {};

        // R0. The native path advances records by the stride the buffer was uploaded with; a
        // declaration that states a different one describes a different record.
        if (declaredStride != nativeRecordStride)
            return "the declaration states a stride of " + std::to_string(declaredStride) +
                   " bytes but the buffer is strided by " + std::to_string(nativeRecordStride) +
                   " bytes, so every record after the first would be read from the wrong address";

        // The renderer has no inferred layout at all for this stride and refuses it on its own. Its
        // established rejection is already deterministic and pre-native; this guard does not
        // replace it.
        if (!inferred.known) return {};

        for (std::size_t i = 0; i < declaredElements.size(); ++i)
        {
            const VertexElement& e = declaredElements[i];
            const int offset = e.getOffsetProperty();
            const VertexElementFormat format = e.getVertexElementFormatProperty();
            const VertexElementUsage usage = e.getVertexElementUsageProperty();
            const int usageIndex = e.getUsageIndexProperty();
            const int size = detail::FormatSize(format);

            // R3. An element must lie wholly inside the record it belongs to.
            if (offset < 0 || size <= 0 || offset + size > declaredStride)
                return "declared element " + detail::Describe(usage, usageIndex, offset, format) +
                       " does not fit inside the declared " + std::to_string(declaredStride) +
                       "-byte record";

            // R4. Two declared elements may not claim the same bytes.
            for (std::size_t j = i + 1; j < declaredElements.size(); ++j)
            {
                const VertexElement& f = declaredElements[j];
                const int otherSize = detail::FormatSize(f.getVertexElementFormatProperty());
                if (detail::RangesOverlap(offset, size, f.getOffsetProperty(), otherSize))
                    return "declared elements " +
                           detail::Describe(usage, usageIndex, offset, format) + " and " +
                           detail::Describe(f.getVertexElementUsageProperty(),
                                            f.getUsageIndexProperty(), f.getOffsetProperty(),
                                            f.getVertexElementFormatProperty()) +
                           " claim the same bytes";
            }

            // R2. The semantic the declaration names must exist natively, at the same offset and
            // in the same format. A native layout that binds the semantic elsewhere, or in another
            // format, reads the caller's bytes as something they are not.
            const InferredVertexElement* match = nullptr;
            for (std::size_t n = 0; n < inferred.count; ++n)
            {
                if (inferred.elements[n].usage == usage &&
                    inferred.elements[n].usageIndex == usageIndex)
                {
                    match = &inferred.elements[n];
                    break;
                }
            }
            if (match == nullptr)
                return "the declaration carries " +
                       detail::Describe(usage, usageIndex, offset, format) +
                       ", which this renderer's native layout for a " +
                       std::to_string(declaredStride) + "-byte record does not bind at all";
            if (match->offset != offset)
                return "the declaration places " +
                       detail::Describe(usage, usageIndex, offset, format) +
                       " but this renderer's native layout reads that semantic at offset " +
                       std::to_string(match->offset);
            if (match->format != format)
                return "the declaration states " +
                       detail::Describe(usage, usageIndex, offset, format) +
                       " but this renderer's native layout reads that semantic as " +
                       detail::FormatName(match->format);
        }

        // R1. No native fetch may read bytes a declared element owns under another identity. R2
        // already covers every semantic the declaration names; this closes the remaining shape --
        // a native attribute the declaration does NOT name reaching into bytes it does.
        for (std::size_t n = 0; n < inferred.count; ++n)
        {
            const InferredVertexElement& native = inferred.elements[n];
            const int nativeSize = detail::FormatSize(native.format);
            for (const VertexElement& e : declaredElements)
            {
                const int size = detail::FormatSize(e.getVertexElementFormatProperty());
                if (!detail::RangesOverlap(native.offset, nativeSize, e.getOffsetProperty(), size))
                    continue;
                if (native.usage == e.getVertexElementUsageProperty() &&
                    native.usageIndex == e.getUsageIndexProperty() &&
                    native.offset == e.getOffsetProperty() &&
                    native.format == e.getVertexElementFormatProperty())
                    continue;
                return "this renderer's native layout reads " +
                       detail::Describe(native.usage, native.usageIndex, native.offset,
                                        native.format) +
                       " out of the bytes the declaration gave to " +
                       detail::Describe(e.getVertexElementUsageProperty(),
                                        e.getUsageIndexProperty(), e.getOffsetProperty(),
                                        e.getVertexElementFormatProperty());
            }
        }

        return {};
    }

    /**
     * @brief Throws unless @p declared is faithfully representable by @p rendererName's layout.
     *
     * Call this before any native layout or pipeline is created, any command is queued and any
     * draw is submitted. It creates nothing, queues nothing and leaves no partial native object
     * behind, so a rejected draw cannot poison the device and the next valid draw still works.
     *
     * @param declared The declaration the buffer carries, as remembered at propagation time.
     * @param nativeRecordStride The stride the renderer advances records by for this draw.
     * @param fallback What this renderer's route does with an unlisted stride.
     * @param rendererName The renderer's public name, for the diagnostic.
     * @param route The route's name (for example `ordinary-indexed`), for the diagnostic.
     * @throws System::NotSupportedException When the declaration cannot be represented faithfully.
     */
    inline void RequireFaithfulVertexDeclaration(
        const DeclaredVertexLayout& declared,
        int nativeRecordStride,
        UnlistedStrideLayout fallback,
        const char* rendererName,
        const char* route)
    {
        if (declared.IsEmpty()) return;
        const InferredVertexLayout inferred =
            InferredLayoutForStride(declared.GetStride(), fallback);
        const std::string failure = DescribeUnrepresentableVertexDeclaration(
            declared.GetElements(), declared.GetStride(), nativeRecordStride, inferred);
        if (failure.empty()) return;
        throw System::NotSupportedException(
            std::string(rendererName) + ": this VertexDeclaration cannot be represented on the " +
            route + " route -- " + failure +
            ". The renderer selects its native vertex layout from the buffer stride and does not "
            "translate arbitrary declarations yet, so the draw is refused rather than rendered "
            "from the wrong bytes.");
    }

    /**
     * @brief Throws unless @p declaredElements can be bound by index to @p inputs.
     *
     * Asymmetric in the same way as the stride rule: only the locations the declaration actually
     * fills are checked. A declaration shorter than the program's input list leaves the remaining
     * inputs unbound, which is a missing input rather than a reinterpretation of declared bytes,
     * and is exactly what a position-only declaration relies on today.
     *
     * @param declaredElements The public declaration's elements, in declaration order.
     * @param inputs The selected stock program's inputs, in attribute-location order.
     * @param inputCount How many inputs @p inputs holds.
     * @param rendererName The renderer's public name, for the diagnostic.
     * @param programName The selected stock program's name, for the diagnostic.
     * @throws System::NotSupportedException When an element would be bound to a location whose
     *         shader input means something else.
     */
    inline void RequireDeclarationMatchesStockProgram(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
        const StockProgramInput* inputs,
        std::size_t inputCount,
        const char* rendererName,
        const char* programName)
    {
        namespace detail = VertexDeclarationFidelityDetail;
        using Microsoft::Xna::Framework::Graphics::VertexElement;

        if (declaredElements.empty() || inputs == nullptr) return;
        const std::size_t checked =
            declaredElements.size() < inputCount ? declaredElements.size() : inputCount;
        for (std::size_t i = 0; i < checked; ++i)
        {
            const VertexElement& e = declaredElements[i];
            const StockProgramInput& in = inputs[i];
            if (in.usage == e.getVertexElementUsageProperty() &&
                in.usageIndex == e.getUsageIndexProperty() &&
                in.format == e.getVertexElementFormatProperty())
                continue;
            throw System::NotSupportedException(
                std::string(rendererName) +
                ": this VertexDeclaration cannot be bound to the stock '" + programName +
                "' program -- element " + std::to_string(i) + " declares " +
                detail::Describe(e.getVertexElementUsageProperty(), e.getUsageIndexProperty(),
                                 e.getOffsetProperty(), e.getVertexElementFormatProperty()) +
                " but that attribute location supplies '" + in.name + "' (" +
                detail::UsageName(in.usage) + std::to_string(in.usageIndex) + ' ' +
                detail::FormatName(in.format) +
                "). Stock programs bind their inputs by attribute location and the declaration's "
                "element order chooses those locations, so the draw is refused rather than "
                "rendered from the wrong attribute.");
        }
    }
}
