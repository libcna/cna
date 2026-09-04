// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

/**
 * @file
 * @brief plans/plan_webgpu.md WEBGPU-155 -- resolving a stock shader's inputs from a vertex
 *        declaration's SEMANTICS rather than from the buffer's byte stride.
 *
 * ## Why this exists
 *
 * A byte stride is not a vertex layout. The same semantic content is legally described at several
 * strides (padding, an unused element, a different vertex struct) and the same stride legally
 * describes several different semantic contents -- 24 bytes is `VertexPositionColorTexture` and also
 * `Position+Normal`; 32 bytes is `VertexPositionNormalTexture` and also a padded `Position+Colour`.
 * A renderer that keys its native input layout on the stride therefore reads some perfectly legal
 * declarations from the wrong bytes, or refuses them outright.
 *
 * XNA identifies a shader input by `(usage, usageIndex)` -- the D3D9 semantic -- and the element's
 * own `offset` and `format` say where and how to read it. Element ORDER inside the declaration is
 * not part of the identity at all: compiled XNB model data routinely orders `TextureCoordinate`
 * before `Normal`. This header turns that rule into one pure, renderer-neutral function so every
 * renderer that adopts it derives the same bindings from the same declaration.
 *
 * ## What it does not do
 *
 * It does not choose WHICH stock program a draw gets -- that decision needs the effect state
 * (`GpuDrawParams`) as well as the declaration and stays with the renderer. It only answers:
 * given a chosen program's ordered input list, where does each input read from?
 *
 * ## The neutral record, and why it is (0, 0, 0, 1)
 *
 * A stock program may declare an input the declaration does not supply -- `DualTextureEffect`'s
 * `TEXCOORD1` on a single-UV mesh, a colour input on a mesh with no colour channel. D3D9, which XNA
 * is defined against, fills the missing components of a vertex register with `(0, 0, 0, 1)`, and
 * OpenGL's disabled generic vertex attribute has exactly the same default value -- which is what
 * the reference renderer (EasyGL) leaves such an input at. So the neutral record here is one
 * `(0, 0, 0, 1)` vector, read at offset 0 with a float format of the input's own width. That is the
 * measured behaviour of the reference, not a convenience: a renderer must not invent a friendlier
 * default (white, say) that would quietly diverge from it.
 */
namespace CNA::Internal::Graphics
{
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    /**
     * @brief The canonical stock-shader inputs, by XNA semantic.
     *
     * One definition per semantic, so a renderer's per-family input lists are assembled from the
     * same constants rather than re-spelled. The names match the reference renderer's attribute
     * names so a diagnostic reads the same on both.
     */
    namespace StockVertexInputsEXT
    {
        /** @brief POSITION0, the only input every stock program consumes. */
        inline constexpr StockProgramInput kPos{
            VertexElementUsage::Position, 0, VertexElementFormat::Vector3, "aPos"};
        /** @brief COLOR0, the vertex colour gated by `VertexColorEnabled`. */
        inline constexpr StockProgramInput kColor{
            VertexElementUsage::Color, 0, VertexElementFormat::Color, "aColor",
            VertexElementFormat::Vector4};
        /** @brief TEXCOORD0. */
        inline constexpr StockProgramInput kUv{
            VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
        /** @brief TEXCOORD1, the second UV set `DualTextureEffect` lightmaps with. */
        inline constexpr StockProgramInput kUv1{
            VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2, "aUV1"};
        /** @brief NORMAL0. */
        inline constexpr StockProgramInput kNormal{
            VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, "aNormal"};
        /** @brief TANGENT0. */
        inline constexpr StockProgramInput kTangent{
            VertexElementUsage::Tangent, 0, VertexElementFormat::Vector4, "aTangent"};
        /** @brief BLENDWEIGHT0. */
        inline constexpr StockProgramInput kWeights{
            VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4, "aBoneWeights"};
        /**
         * @brief BLENDINDICES0.
         *
         * `Vector4` is as legal a spelling as `Byte4` -- the format describes the bytes, the shader
         * register is a float4 either way -- and a content processor may write either.
         */
        inline constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Byte4, "aBoneIndices",
            VertexElementFormat::Vector4};
    }

    /** @brief The most inputs any stock program in this project declares. */
    inline constexpr std::size_t kMaxStockVertexAttributes = 8;

    /**
     * @brief The neutral vertex record a defaulted input reads from: one `(0, 0, 0, 1)` vector.
     *
     * Sixteen bytes, bound as a whole vertex buffer whose array stride is zero, so every vertex
     * reads the same value. See this file's header comment for why `(0, 0, 0, 1)` and not anything
     * more convenient.
     */
    inline constexpr std::array<float, 4> kNeutralVertexRecordEXT{0.0f, 0.0f, 0.0f, 1.0f};

    /**
     * @brief The float format a defaulted input of @p expected width reads the neutral record with.
     *
     * The neutral record is floats, so a `Color`-spelled input is read as `Vector4` rather than as
     * a packed colour -- the shader sees a `vec4f` either way.
     *
     * @param expected The stock input's declared format.
     * @return The float format of the same component count.
     */
    [[nodiscard]] inline VertexElementFormat NeutralFormatForStockInputEXT(
        VertexElementFormat expected) noexcept
    {
        switch (expected)
        {
        case VertexElementFormat::Single:
            return VertexElementFormat::Single;
        case VertexElementFormat::Vector2:
        case VertexElementFormat::HalfVector2:
        case VertexElementFormat::Short2:
        case VertexElementFormat::NormalizedShort2:
            return VertexElementFormat::Vector2;
        case VertexElementFormat::Vector3:
            return VertexElementFormat::Vector3;
        default:
            return VertexElementFormat::Vector4;
        }
    }

    /**
     * @brief One stock shader input, resolved against a declaration.
     */
    struct ResolvedStockAttributeEXT
    {
        /** @brief The semantic this input consumes. */
        VertexElementUsage usage{};
        /** @brief The semantic's usage index. */
        int usageIndex = 0;
        /** @brief The format to read with -- the DECLARED format, or the neutral float format. */
        VertexElementFormat format{};
        /** @brief The byte offset to read from, within the vertex record or the neutral record. */
        int offset = 0;
        /** @brief The shader location, which is this input's index in the program's input list. */
        int shaderLocation = 0;
        /** @brief True when the declaration does not name this semantic and the neutral record supplies it. */
        bool defaulted = false;
    };

    /**
     * @brief A stock program's whole input list, resolved against one declaration.
     */
    struct ResolvedStockVertexLayoutEXT
    {
        /** @brief The resolved inputs, in shader-location order. */
        std::array<ResolvedStockAttributeEXT, kMaxStockVertexAttributes> attributes{};
        /** @brief How many entries of @ref attributes are valid. */
        std::size_t count = 0;
        /** @brief The vertex record's byte stride -- the only thing the stride is still used for. */
        int stride = 0;
        /** @brief Whether any input fell back to the neutral record. */
        bool usesNeutralRecord = false;
        /** @brief Whether this layout came from a real declaration at all. */
        bool fromDeclaration = false;
    };

    /**
     * @brief Finds the declaration element carrying one semantic.
     *
     * @param declaredElements The declaration's elements, in whatever order it lists them.
     * @param usage The semantic to find.
     * @param usageIndex The semantic's usage index.
     * @return The element, or null when the declaration does not name it.
     */
    [[nodiscard]] inline const VertexElement* FindDeclaredSemanticEXT(
        const std::vector<VertexElement>& declaredElements,
        VertexElementUsage usage, int usageIndex) noexcept
    {
        for (const VertexElement& element : declaredElements)
        {
            if (element.getVertexElementUsageProperty() == usage &&
                element.getUsageIndexProperty() == usageIndex)
                return &element;
        }
        return nullptr;
    }

    /**
     * @brief Whether the declaration names a semantic at any usage index.
     *
     * The question a renderer's stock-program SELECTION asks -- "is this a lit vertex?" is "does the
     * declaration name a Normal?", never "is the stride 32?".
     *
     * @param declaredElements The declaration's elements.
     * @param usage The semantic to look for.
     * @return True when at least one element carries @p usage.
     */
    [[nodiscard]] inline bool DeclarationNamesUsageEXT(
        const std::vector<VertexElement>& declaredElements, VertexElementUsage usage) noexcept
    {
        for (const VertexElement& element : declaredElements)
        {
            if (element.getVertexElementUsageProperty() == usage) return true;
        }
        return false;
    }

    /**
     * @brief Resolves a stock program's inputs against a declaration, by semantic.
     *
     * Each input is located by `(usage, usageIndex)` and bound at the declared element's OWN offset
     * and format; an input the declaration does not name is bound to the neutral record instead.
     * Declaration order and byte offsets are deliberately unrestricted, and elements the program
     * does not consume are ignored -- both are legal in XNA and both occur in real XNB model data.
     *
     * Format compatibility is NOT checked here: `RequireDeclarationMatchesStockProgram` in
     * `VertexDeclarationFidelity.hpp` is the one place that raises that diagnostic, and a renderer
     * calls it before resolving so a rejected draw creates nothing.
     *
     * @param declaredElements The declaration's elements.
     * @param stride The declaration's byte stride, which becomes the native array stride.
     * @param inputs The selected stock program's inputs, in shader-location order.
     * @param inputCount How many entries @p inputs holds.
     * @return The resolved layout; @c fromDeclaration is false when @p declaredElements is empty.
     */
    [[nodiscard]] inline ResolvedStockVertexLayoutEXT ResolveStockVertexLayoutEXT(
        const std::vector<VertexElement>& declaredElements,
        int stride,
        const StockProgramInput* inputs,
        std::size_t inputCount)
    {
        ResolvedStockVertexLayoutEXT resolved;
        resolved.stride = stride;
        if (inputs == nullptr || inputCount == 0) return resolved;
        if (inputCount > kMaxStockVertexAttributes) inputCount = kMaxStockVertexAttributes;
        resolved.count = inputCount;
        resolved.fromDeclaration = !declaredElements.empty();

        for (std::size_t location = 0; location < inputCount; ++location)
        {
            const StockProgramInput& input = inputs[location];
            ResolvedStockAttributeEXT& attribute = resolved.attributes[location];
            attribute.usage = input.usage;
            attribute.usageIndex = input.usageIndex;
            attribute.shaderLocation = static_cast<int>(location);

            const VertexElement* element = resolved.fromDeclaration
                ? FindDeclaredSemanticEXT(declaredElements, input.usage, input.usageIndex)
                : nullptr;
            if (element != nullptr)
            {
                attribute.format = element->getVertexElementFormatProperty();
                attribute.offset = element->getOffsetProperty();
                attribute.defaulted = false;
            }
            else
            {
                attribute.format = NeutralFormatForStockInputEXT(input.format);
                attribute.offset = 0;
                attribute.defaulted = true;
                resolved.usesNeutralRecord = true;
            }
        }
        return resolved;
    }

    /**
     * @brief A stable 64-bit digest of a resolved layout, for a pipeline cache key.
     *
     * Two draws whose declarations resolve to the same bindings must share one native pipeline, and
     * two that resolve differently must not -- which is exactly what a stride-keyed cache got wrong.
     *
     * @param layout The resolved layout.
     * @return The digest.
     */
    [[nodiscard]] inline std::uint64_t HashResolvedStockVertexLayoutEXT(
        const ResolvedStockVertexLayoutEXT& layout) noexcept
    {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        const auto mix = [&hash](std::uint64_t value) noexcept
        {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        mix(static_cast<std::uint64_t>(layout.stride));
        mix(layout.count);
        mix(layout.usesNeutralRecord ? 1u : 0u);
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            const ResolvedStockAttributeEXT& a = layout.attributes[i];
            mix(static_cast<std::uint64_t>(a.offset));
            mix(static_cast<std::uint64_t>(a.format));
            mix(static_cast<std::uint64_t>(a.shaderLocation));
            mix(a.defaulted ? 1u : 0u);
        }
        return hash;
    }
}
