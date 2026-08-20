// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_gltf.md GLTF-473: the shared refusal for a fixed-function renderer about to read one
// layout's bytes through another layout's rule.
//
// A renderer with a programmable pipeline binds a vertex attribute per declared element, so a
// record it cannot describe simply has no attribute. A FIXED-FUNCTION renderer has no such freedom:
// `glColorPointer`/`glTexCoordPointer`/`glNormalPointer` each take one offset, and that offset is
// usually written as a literal beside the call. The literal is right for the layouts the route was
// designed for and silently wrong for every other one -- and "silently" is exact, because a
// four-byte colour read from the middle of a float normal is a perfectly valid RGBA value.
//
// That is what GLTF-473 was: `OPENGLES1` routed every draw it has no fixed-function equivalent for
// (PbrEffect, SkinnedEffect, a custom ShaderEffect, instancing, a dual-texture or environment-map
// draw whose preconditions were not met) into its colour path, which binds a colour at offset 12.
// Offset 12 carries a colour in exactly two of CNA's canonical records -- stride 16 and stride 24 --
// and carries the NORMAL in every PBR and skinned one. A valid core glTF vertex-coloured
// metallic-roughness primitive (stride 60) was therefore drawn with per-vertex colours read from the
// bytes of its own normals: accepted input, incorrect semantics, which is the one outcome the
// GLTF-465 partition forbids.
//
// The rule below is the general form, asked of the canonical table rather than of a literal: every
// client array a route enables must sit where THAT STRIDE's record actually carries the semantic. It
// abstains for a stride the table does not list, which is a renderer-local record this file has no
// opinion about, and it is deliberately a refusal rather than a correction -- re-deriving the offset
// would change what such a draw renders, and the renderers this guards cannot be exercised
// everywhere the guard is compiled.

#include <cstddef>
#include <string>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Internal::Renderers
{
    /**
     * @brief True when @p boundOffset is where @p strideInBytes really carries that semantic.
     *
     * Abstains -- returns true -- for a stride the canonical table does not list, because such a
     * record is a renderer's own and this file has no offset to compare against.
     *
     * @param strideInBytes The record stride the route strides the buffer by.
     * @param usage The semantic the client array supplies.
     * @param usageIndex The semantic's usage index.
     * @param boundOffset The byte offset the route is about to pass to the pointer call.
     * @return False only when the stride's canonical layout definitely puts that semantic
     *         elsewhere, or does not carry it at all.
     */
    [[nodiscard]] inline bool FixedFunctionClientArrayMatchesCanonicalLayoutEXT(
        int strideInBytes,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
        int usageIndex,
        int boundOffset) noexcept
    {
        const CNA::Internal::Graphics::CanonicalSemanticOffsetEXT canonical =
            CNA::Internal::Graphics::CanonicalOffsetOfSemanticEXT(strideInBytes, usage, usageIndex);
        if (!canonical.strideKnown) { return true; }
        return canonical.present && canonical.offset == boundOffset;
    }

    /**
     * @brief Refuses a draw whose client array would read a semantic the record keeps elsewhere.
     *
     * Call it before any native state is touched, once per client array the route is about to
     * enable. The message names the renderer, the route, the semantic, the offset that would have
     * been read, what the record actually carries there, `GLTF-473`, and -- when the draw reached
     * this route through an unsupported effect rather than through a wrong buffer -- that effect and
     * the way out.
     *
     * @param strideInBytes The record stride the route strides the buffer by.
     * @param usage The semantic the client array supplies.
     * @param usageIndex The semantic's usage index.
     * @param boundOffset The byte offset the route is about to pass to the pointer call.
     * @param arrayName The pointer call being set up, e.g. "glColorPointer".
     * @param rendererName Short renderer identifier, e.g. "OPENGLES1".
     * @param route Name of the draw route, for the diagnostic.
     * @param unsupportedSemantic The effect or feature that sent the draw down this route, or null
     *        when the caller bound the buffer to this route directly.
     * @throws System::NotSupportedException When the array would read the wrong bytes.
     */
    inline void RequireFixedFunctionClientArrayEXT(
        int strideInBytes,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
        int usageIndex,
        int boundOffset,
        const char* arrayName,
        const char* rendererName,
        const char* route,
        const char* unsupportedSemantic)
    {
        if (FixedFunctionClientArrayMatchesCanonicalLayoutEXT(strideInBytes, usage, usageIndex,
                                                              boundOffset))
        {
            return;
        }

        namespace detail = CNA::Internal::Graphics::VertexDeclarationFidelityDetail;
        const CNA::Internal::Graphics::CanonicalSemanticOffsetEXT canonical =
            CNA::Internal::Graphics::CanonicalOffsetOfSemanticEXT(strideInBytes, usage, usageIndex);

        // What the record really keeps at the offset that was about to be read. Naming it is the
        // difference between "unsupported layout" and a diagnostic somebody can act on.
        std::string occupant = "nothing this layout declares";
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                strideInBytes, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            if (layout.elements[i].offset == boundOffset)
            {
                occupant = std::string(detail::UsageName(layout.elements[i].usage)) +
                           std::to_string(layout.elements[i].usageIndex);
                break;
            }
        }

        std::string message =
            std::string(rendererName) + " renderer: the " + route + " route would bind " +
            arrayName + " for " + detail::UsageName(usage) + std::to_string(usageIndex) +
            " at byte offset " + std::to_string(boundOffset) + " of a stride-" +
            std::to_string(strideInBytes) + " vertex record, but that record carries " + occupant +
            " there";
        if (canonical.present)
        {
            message += " and keeps " + std::string(detail::UsageName(usage)) +
                       std::to_string(usageIndex) + " at offset " +
                       std::to_string(canonical.offset);
        }
        message +=
            ". Reading it anyway would reinterpret one layout's bytes through another's rule and "
            "produce a plausible but wrong surface reported as a successful draw, so the draw is "
            "refused instead (plans/plan_gltf.md GLTF-473).";
        if (unsupportedSemantic != nullptr)
        {
            message += " This draw reached a fixed-function route because ";
            message += unsupportedSemantic;
            message +=
                " has no fixed-function equivalent on this renderer. Use a renderer that implements "
                "it, or draw this geometry with an effect this renderer supports.";
        }
        throw System::NotSupportedException(message);
    }
}
