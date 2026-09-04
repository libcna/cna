// SPDX-License-Identifier: MS-PL
#pragma once

#include <vulkan/vulkan.h>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanVertexFormatHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace CNA::Internal::Renderers::Vulkan
{
    /**
     * @brief The `VkVertexInputAttributeDescription` set one stock program needs from one
     *        `VertexDeclaration`, plus a hash that identifies it to the pipeline cache.
     *
     * plans/plan_vulkan.md `VULKAN-145`, the shared half of `VULKAN-144`.
     *
     * Every 3D pipeline factory in this renderer bakes its attribute array from the buffer's byte
     * STRIDE, through a ten-entry table. That is why a 28-byte record carrying
     * `TextureCoordinate0@12 Vector2` is refused outright: the stride says "28", the table has no
     * 28, and nothing in the pipeline knows where the UV actually is. The declaration knows. This
     * builds the attribute set from it.
     *
     * Matching is by **semantic** -- `(usage, usageIndex)` -- and each matched element contributes
     * its own byte offset and its own format. Declaration ORDER is therefore irrelevant, which is
     * the property that makes this different from EasyGL's `ApplyLayout` (location = the element's
     * index in the list) and is exactly what `VertexDeclarationFidelity.hpp`'s own contract
     * permits: *"XNA vertex declarations identify shader inputs by usage and usage index ...
     * A renderer that maps semantics to locations may bind either order faithfully."* Compiled XNB
     * model data routinely orders `TextureCoordinate` before `Normal`.
     */
    struct VulkanVertexInputLayoutEXT
    {
        /// Vulkan's own floor for `maxVertexInputAttributes`; no stock program here comes close.
        static constexpr std::size_t kMaxAttributes = 16;

        /** @brief One description per shader input the declaration supplied, in location order. */
        std::array<VkVertexInputAttributeDescription, kMaxAttributes> attributes{};
        /** @brief How many of @ref attributes are filled. */
        std::uint32_t attributeCount = 0;
        /**
         * @brief Locations the program consumes that the declaration did not supply, as a bit set.
         *
         * Reported rather than silently skipped. A Vulkan shader input with no attribute
         * description reads undefined data, so whether to refuse the draw or fall back is the
         * caller's decision and this must not make it for them.
         */
        std::uint32_t missingInputMask = 0;
        /**
         * @brief Element formats the declaration used that this renderer has no `VkFormat` for.
         *
         * Also a bit set over input locations. Separate from @ref missingInputMask because the two
         * mean different things to a caller: one is a declaration that said nothing, the other one
         * that said something unrepresentable.
         */
        std::uint32_t unrepresentableInputMask = 0;
        /** @brief Whether the declaration was empty, i.e. this layout has no opinion at all. */
        bool empty = true;

        /** @brief True when every consumed input was supplied and representable. */
        [[nodiscard]] bool IsComplete() const noexcept
        {
            return !empty && missingInputMask == 0 && unrepresentableInputMask == 0;
        }

        /**
         * @brief A stable identity for this attribute set, for folding into a pipeline-cache key.
         *
         * Two declarations of the SAME stride whose elements sit at different offsets must not
         * share a pipeline, which is the defect the stride table has. Hashes only what the pipeline
         * actually bakes -- location, format and offset, in location order -- so a declaration
         * reordered without moving anything hashes identically, and one that moves a byte does not.
         *
         * @return The hash; 0 for an empty layout, which no non-empty one can produce because
         *         every seeded round mixes in a non-zero location term.
         */
        [[nodiscard]] std::uint64_t Hash() const noexcept
        {
            if (empty) return 0;
            std::uint64_t h = 0xcbf29ce484222325ull;   // FNV-1a offset basis
            const auto mix = [&h](std::uint64_t v) {
                h ^= v;
                h *= 0x100000001b3ull;
            };
            mix(attributeCount + 1u);
            for (std::uint32_t i = 0; i < attributeCount; ++i) {
                mix(attributes[i].location + 1u);
                mix(static_cast<std::uint64_t>(attributes[i].format));
                mix(attributes[i].offset + 1u);
            }
            mix(missingInputMask + 1u);
            mix(unrepresentableInputMask + 1u);
            return h;
        }
    };

    /**
     * @brief Builds the attribute set @p inputs needs from @p declared.
     *
     * plans/plan_vulkan.md `VULKAN-145`. Location `i` is `inputs[i]`, matching this project's
     * established "`layout(location = N)` is the Nth field of the ported HLSL input struct"
     * convention -- the same one every hand-written shader under
     * `modules/renderers/vulkan/src/shaders/` already follows.
     *
     * An input the declaration does not supply produces **no** attribute description and a bit in
     * `missingInputMask`; it is never given a made-up offset, because a wrong offset renders wrong
     * pixels while a missing one is a question the caller can answer.
     *
     * @param declared   The declaration the buffer carries; an empty one yields an empty layout.
     * @param inputs     The selected stock program's inputs, in attribute-location order.
     * @param inputCount How many inputs @p inputs holds.
     * @return The layout.
     */
    [[nodiscard]] inline VulkanVertexInputLayoutEXT BuildVulkanVertexInputLayoutEXT(
        const CNA::Internal::Graphics::DeclaredVertexLayout& declared,
        const CNA::Internal::Graphics::StockProgramInput* inputs,
        std::size_t inputCount)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;

        VulkanVertexInputLayoutEXT layout;
        if (declared.IsEmpty() || inputs == nullptr || inputCount == 0)
            return layout;
        layout.empty = false;

        const std::size_t consumed =
            inputCount < VulkanVertexInputLayoutEXT::kMaxAttributes
                ? inputCount
                : VulkanVertexInputLayoutEXT::kMaxAttributes;

        for (std::size_t location = 0; location < consumed; ++location)
        {
            const CNA::Internal::Graphics::StockProgramInput& in = inputs[location];
            const VertexElement* match = nullptr;
            for (const VertexElement& e : declared.GetElements())
            {
                if (e.getVertexElementUsageProperty() == in.usage &&
                    e.getUsageIndexProperty() == in.usageIndex)
                {
                    match = &e;
                    break;
                }
            }
            if (match == nullptr) {
                layout.missingInputMask |= (1u << location);
                continue;
            }

            const VkFormat format =
                VertexElementFormatToVk(match->getVertexElementFormatProperty());
            if (format == VK_FORMAT_UNDEFINED) {
                layout.unrepresentableInputMask |= (1u << location);
                continue;
            }

            VkVertexInputAttributeDescription& attr = layout.attributes[layout.attributeCount++];
            attr.location = static_cast<std::uint32_t>(location);
            attr.binding  = 0;
            attr.format   = format;
            attr.offset   = static_cast<std::uint32_t>(match->getOffsetProperty());
        }
        return layout;
    }

    /**
     * @brief Names, for a diagnostic, the inputs @p layout could not fill.
     *
     * Kept beside the builder so a refusal message says which shader input went unsupplied rather
     * than only that something did.
     *
     * @param layout     A layout from BuildVulkanVertexInputLayoutEXT.
     * @param inputs     The same input table it was built against.
     * @param inputCount How many inputs @p inputs holds.
     * @return A comma-separated list of attribute names, or an empty string when nothing is
     *         missing or unrepresentable.
     */
    [[nodiscard]] inline std::string DescribeVulkanVertexInputGapsEXT(
        const VulkanVertexInputLayoutEXT& layout,
        const CNA::Internal::Graphics::StockProgramInput* inputs,
        std::size_t inputCount)
    {
        std::string out;
        const std::uint32_t gaps = layout.missingInputMask | layout.unrepresentableInputMask;
        for (std::size_t location = 0; location < inputCount && location < 32u; ++location)
        {
            if ((gaps & (1u << location)) == 0) continue;
            if (!out.empty()) out += ", ";
            out += inputs[location].name;
            out += (layout.missingInputMask & (1u << location)) ? " (not declared)"
                                                                : " (format unsupported here)";
        }
        return out;
    }
} // namespace CNA::Internal::Renderers::Vulkan
