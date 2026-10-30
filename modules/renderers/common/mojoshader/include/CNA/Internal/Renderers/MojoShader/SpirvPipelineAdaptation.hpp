// SPDX-License-Identifier: MS-PL
//
// Adaptations of MojoShader's SPIR-V to the CNA pipeline that draws it, each a bounded rewrite of
// something MojoShader always emits in one shape.
//
// vFace. Direct3D 9's `vFace` is positive on a front face, and an XNA front face is one wound
// clockwise as displayed. MojoShader's SPIR-V profile derives the sign from the `FrontFacing`
// built-in, which is right for a pipeline whose native front face is that same winding (FNA3D's
// SDLGPU driver makes its front face clockwise). CNA's SDLGPU and WebGPU pipelines keep
// counter-clockwise as the native front face and map culling and two-sided stencil onto it
// instead, so there `FrontFacing` is true on XNA's BACK face. The one place MojoShader turns
// `FrontFacing` into `vFace` is
//
//     %ff  = OpLoad %bool %gl_FrontFacing
//     %tmp = OpSelect %float %ff %float_1 %float_n1
//
// and the rewrite swaps that select's two results.
//
// Unattached colour outputs. A pixel shader may write `oC2` while only one or two render targets
// are bound; Direct3D 9 discards the write. A Vulkan pipeline whose fragment shader writes a
// location with no colour attachment is legal too, but reported by the validation layer, so the
// outputs at and beyond the bound count become private variables: still written, never read.
//
// Pixel centres. XNA inherits Direct3D 9's integer pixel centres, so a renderer whose API puts them
// on half pixels moves filled geometry just under half a pixel right and down. The stock programs
// fold that into their matrix; a compiled vertex shader computes its own clip position, and where
// the viewport cannot carry the shift (a WebGPU viewport must lie inside its attachment) the end of
// the vertex entry point adds it: `Position.xy += offset * Position.w`.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    /** @brief Result of adapting a fragment module to its pipeline. */
    struct SpirvPipelineAdaptationResult
    {
        /** @brief The rewritten SPIR-V, or the input unchanged when nothing needed rewriting. */
        std::vector<std::uint32_t> words;
        /** @brief Whether the module changed. */
        bool changed = false;
        /** @brief Non-empty when the module could not be walked or has an unexpected shape. */
        std::string error;
    };

    /**
     * @brief Mirrors the sign MojoShader gives `vFace`, for a pipeline whose native front face is
     * counter-clockwise.
     *
     * @param words SPIR-V words of a fragment module.
     * @param wordCount Number of 32-bit words at @p words.
     * @return The rewritten module. When a load of `FrontFacing` is not the condition of an
     *         `OpSelect`, `error` is set and the input is returned unchanged, so the caller refuses
     *         rather than draws with the wrong sign.
     */
    [[nodiscard]] SpirvPipelineAdaptationResult MirrorFrontFacing(const std::uint32_t* words,
                                                                std::size_t wordCount);

    /**
     * @brief Turns every colour output at a location of @p attachedCount or above into a private
     * variable, so the module writes only the attachments its pass has.
     *
     * @param words SPIR-V words of a fragment module.
     * @param wordCount Number of 32-bit words at @p words.
     * @param attachedCount Number of colour attachments the pass binds, from location 0.
     * @return The rewritten module. `error` is set, and the input returned, for a module it cannot
     *         walk or an output used other than through `OpLoad`/`OpStore`.
     */
    [[nodiscard]] SpirvPipelineAdaptationResult DropUnattachedColorOutputs(
        const std::uint32_t* words, std::size_t wordCount, std::uint32_t attachedCount);

    /**
     * @brief Adds @p offsetX and @p offsetY, scaled by w, to a vertex module's clip position when
     * its entry point returns.
     *
     * @param words SPIR-V words of a vertex module.
     * @param wordCount Number of 32-bit words at @p words.
     * @param offsetX Clip-space x offset per unit of w.
     * @param offsetY Clip-space y offset per unit of w.
     * @return The rewritten module, unchanged for a zero offset. `error` is set, and the input
     *         returned, for a module without exactly one `Position` output or its types.
     */
    [[nodiscard]] SpirvPipelineAdaptationResult OffsetClipPosition(const std::uint32_t* words,
                                                                   std::size_t wordCount,
                                                                   float offsetX, float offsetY);
}
