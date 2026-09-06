// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-208: make `SamplerState.MipMapLevelOfDetailBias` reach a compiled XNA
// Effect's texture sampling.
//
// XNA applies the bias to the level of detail an implicit sample computes. Direct3D 9 carried it as
// sampler state (`D3DSAMP_MIPMAPLODBIAS`), so a compiled effect's `sampler_state` block can set it
// per register and MojoShader publishes it -- but MojoShader emits an ordinary implicit sample, and
// no WebGPU sampler descriptor can hold the value: `WGPUSamplerDescriptor` has `lodMinClamp` and
// `lodMaxClamp` and no bias field at all. The value has to reach the SHADER.
//
// It reaches it here, once, BEFORE the native and browser routes diverge. Both targets consume the
// SPIR-V this file produces -- natively through `WGPUShaderSourceSPIRV`, in a browser through
// `SpirvToWgsl` -- so the XNA semantic cannot differ between them by construction. `WEBGPU-205`
// made the same choice for the stock effects and put the value in a per-draw uniform; this is that
// idea applied to a shader CNA does not author.
//
// The rewrite is bounded, and every id it needs is already in the module:
//
//     %tmp  = OpSampledImage %si %ti %ts                  ; the split's own output
//     %res  = OpImageSampleImplicitLod %v4float %tmp %coord
//
// becomes
//
//     %arr    = OpTypeArray %v4float %uint_16             ; ArrayStride 16
//     %blk    = OpTypeStruct %arr                         ; Block, member 0 Offset 0
//     %bptr   = OpTypePointer Uniform %blk
//     %bias   = OpVariable %bptr Uniform                  ; DescriptorSet S, Binding B
//     %fptr   = OpTypePointer Uniform %float
//     ...
//     %p      = OpAccessChain %fptr %bias %uint_0 %uint_slot %uint_0
//     %b      = OpLoad %float %p
//     %res    = OpImageSampleImplicitLod %v4float %tmp %coord Bias %b
//
// Three properties are deliberate:
//
//   * **The slot index is a CONSTANT at each sample site**, resolved from the sampler variable the
//     sample loaded, so two samplers with different biases in one shader cannot be conflated. There
//     is no dynamic indexing and no per-shader "the" bias.
//   * **The bias is uniform DATA, not shader structure.** Changing a bias value rewrites a buffer;
//     it never rebuilds a module or a pipeline. The module is identical for every bias.
//   * **Zero bias is exactly the old behaviour.** `textureSampleBias(t, s, c, 0.0)` and
//     `textureSample(t, s, c)` select the same level, so a pass that sets no bias is unaffected.
//
// A `vec4` array rather than a `float` array because std140 gives an array of scalars a 16-byte
// stride anyway; spelling it as `vec4` makes the layout the same on both sides of the translator
// and keeps `SpirvToWgsl`'s existing uniform handling unchanged. Only `.x` of each element is read.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    /** @brief How many D3D9 sampler registers the injected bias block carries. */
    inline constexpr std::uint32_t kSpirvLodBiasSlotCount = 16u;

    /** @brief Bytes the bias uniform block occupies: one `vec4` per sampler register. */
    inline constexpr std::uint32_t kSpirvLodBiasBlockBytes = kSpirvLodBiasSlotCount * 16u;

    /** @brief Result of injecting per-sampler LOD bias into a module. */
    struct SpirvLodBiasResult
    {
        /** @brief The rewritten SPIR-V, or the input unchanged when nothing needed rewriting. */
        std::vector<std::uint32_t> words;
        /** @brief D3D9 sampler registers whose samples now consume a bias, ascending. */
        std::vector<std::uint32_t> biasedSlots;
        /** @brief Whether the module gained a bias block. */
        bool changed = false;
        /** @brief Non-empty when the module could not be walked; `words` is then the input. */
        std::string error;
    };

    /**
     * @brief Makes every implicit image sample in @p words consume its sampler's LOD bias.
     *
     * Samples whose sampler register cannot be identified are left exactly as they were, so a
     * module this transformation does not fully understand still renders what it rendered before.
     *
     * @param words SPIR-V words, already through `SplitCombinedImageSamplers`.
     * @param wordCount Number of 32-bit words at @p words.
     * @param descriptorSet Descriptor set to put the bias block in; use the stage's own sampler set
     *        so no new bind group is needed (WebGPU allows only four).
     * @param binding Binding within that set. Must not collide with the split's doubled sampler
     *        bindings, which occupy `2*register` and `2*register + 1`.
     * @return The rewritten module and the registers it biased. On a malformed module `error` is
     *         set and the input is returned unchanged.
     */
    [[nodiscard]] SpirvLodBiasResult InjectSamplerLodBias(const std::uint32_t* words,
                                                          std::size_t wordCount,
                                                          std::uint32_t descriptorSet,
                                                          std::uint32_t binding);
}
