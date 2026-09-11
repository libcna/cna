// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-166/167: split SPIR-V combined image samplers into the separate image and
// sampler pair WebGPU's shading model requires.
//
// MojoShader's "spirv" (Vulkan-mode) profile emits one `OpTypeSampledImage` global per D3D9
// sampler register -- Vulkan's VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER shape. WGSL has no such
// type: a texture binding and a sampler binding are always two separate resources, and naga's
// SPIR-V frontend rejects a load of a combined global with `invalid id %N`. Everything else
// MojoShader emits passes naga untouched, so the whole gap between the two is this one construct.
//
// The rewrite is bounded because MojoShader's emission pattern is fixed:
//
//     %si   = OpTypeSampledImage %img
//     %ptr  = OpTypePointer UniformConstant %si
//     %var  = OpVariable %ptr UniformConstant          ; DescriptorSet S, Binding B
//     ...
//     %tmp  = OpLoad %si %var
//     %res  = OpImageSample*Lod %vec4 %tmp %coord
//
// becomes
//
//     %si       = OpTypeSampledImage %img              ; kept: OpSampledImage still yields it
//     %smpT     = OpTypeSampler
//     %imgPtr   = OpTypePointer UniformConstant %img
//     %smpPtr   = OpTypePointer UniformConstant %smpT
//     %imgVar   = OpVariable %imgPtr UniformConstant   ; DescriptorSet S, Binding 2*B
//     %smpVar   = OpVariable %smpPtr UniformConstant   ; DescriptorSet S, Binding 2*B + 1
//     ...
//     %ti       = OpLoad %img %imgVar
//     %ts       = OpLoad %smpT %smpVar
//     %tmp      = OpSampledImage %si %ti %ts           ; SAME result id, so no use site moves
//
// Keeping the original `OpLoad`'s result id is what makes this a local edit: every downstream
// instruction that consumed the combined value still consumes it, and no id renumbering is needed.
//
// The doubled binding numbers are this file's own convention, not MojoShader's: WebGPU needs two
// binding slots where Vulkan needed one, and a renderer that builds its own bind-group layout is
// free to choose them as long as it chooses the same ones the shader was rewritten with. That is
// what SplitResult::samplers reports back.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    /** @brief One D3D9 sampler register after the split, and the two bindings it now occupies. */
    struct SpirvSplitSamplerBinding
    {
        /** @brief Descriptor set the combined global carried (MojoShader: 0 vertex, 2 pixel). */
        std::uint32_t set = 0;
        /** @brief Binding the combined global carried; equal to the D3D9 sampler register. */
        std::uint32_t originalBinding = 0;
        /** @brief Binding the texture half now occupies. */
        std::uint32_t textureBinding = 0;
        /** @brief Binding the sampler half now occupies. */
        std::uint32_t samplerBinding = 0;
        /** @brief SPIR-V `Dim` of the image: 1D=0, 2D=1, 3D=2, Cube=3. */
        std::uint32_t dim = 0;
        /** @brief Whether the image was declared as an array (`OpTypeImage` Arrayed operand). */
        bool arrayed = false;
        /** @brief Name MojoShader gave the combined global, e.g. "ps_s0". */
        std::string name;
    };

    /** @brief Rewritten module plus the binding map a bind-group layout must be built from. */
    struct SpirvSplitResult
    {
        /** @brief The rewritten SPIR-V, or the input unchanged when nothing needed splitting. */
        std::vector<std::uint32_t> words;
        /** @brief One entry per combined global that was split, in ascending id order. */
        std::vector<SpirvSplitSamplerBinding> samplers;
        /** @brief Whether any rewrite happened. */
        bool changed = false;
        /** @brief Non-empty when the module could not be walked; `words` is then the input. */
        std::string error;
    };

    /**
     * @brief Rewrites every combined image sampler in a SPIR-V module into an image/sampler pair.
     *
     * @param words SPIR-V words as MojoShader emitted them, with the trailing patch table already
     *        trimmed off (`output_len` minus `MOJOSHADER_linkSPIRVShaders`'s return value).
     * @param wordCount Number of 32-bit words at @p words.
     * @return The rewritten module, the resulting bindings, and whether anything changed. On a
     *         malformed module `error` is set and the input is returned unchanged.
     */
    [[nodiscard]] SpirvSplitResult SplitCombinedImageSamplers(const std::uint32_t* words,
                                                              std::size_t wordCount);
}
