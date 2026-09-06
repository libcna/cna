// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-203: translate the SPIR-V this repository's compiled-effect pipeline
// emits into WGSL, so browser WebGPU can run a compiled XNA Effect.
//
// WHY THIS EXISTS, AND WHY IT IS NOT A GENERAL SHADER TRANSLATOR
//
// Native WebGPU takes SPIR-V through a chained `WGPUShaderSourceSPIRV`. Browser WebGPU does not:
// SPIR-V is not part of the WebGPU specification, and emdawnwebgpu's `createShaderModule` has a
// single chained-struct case, `ShaderSourceWGSL`. So the browser needs the same program expressed
// in WGSL, and the only place the two routes may diverge is the shader-module representation --
// everything above it (Effect parsing, MojoShader invocation, reflection, constants, samplers,
// techniques, vertex declarations, parameter snapshots, SpriteBatch, multi-pass) is shared.
//
// The input is NOT arbitrary Vulkan SPIR-V. It is what
// `MOJOSHADER_parse(MOJOSHADER_PROFILE_SPIRV)` + `MOJOSHADER_linkSPIRVShaders` +
// `SplitCombinedImageSamplers` produce, which is generated code with a fixed and small repertoire.
// Measured across all nine committed compiled-effect fixtures (27 technique/pass pairs, 54 shader
// modules) with `spikes/webgpu-spirv-spike`:
//
//   * 55 distinct opcodes and 9 GLSL.std.450 instructions;
//   * five storage classes: UniformConstant, Input, Uniform, Output, Private;
//   * seven decorations: Block, ArrayStride, BuiltIn, Location, Binding, DescriptorSet, Offset;
//   * exactly ONE builtin, `Position`;
//   * no `OpTypeMatrix`, no `OpPhi`, no `OpLoopMerge`, no `OpSwitch`, no `OpFunctionCall`, and one
//     function per module.
//
// This file therefore implements a real SPIR-V module parser and typed IR over that subset, and
// REFUSES BY NAME anything outside it. It is deliberately not extended "just in case": an opcode
// nobody can produce is an opcode nobody can test.
//
// THE ONE STRUCTURAL DIFFERENCE BETWEEN THE TWO LANGUAGES
//
// SPIR-V models shader I/O as memory -- `Input`/`Output` global variables that the body loads from
// and stores to. WGSL models it as entry-point parameters and a return value. Rather than rewrite
// every load and store, the emitter keeps the body verbatim against `var<private>` shadows of each
// I/O variable and wraps it:
//
//     var<private> ps_v0 : vec4<f32>;                     // was Input
//     var<private> ps_oC0 : vec4<f32>;                    // was Output
//     fn ShaderFunction9_body() { ...unchanged body... }
//     @fragment fn ShaderFunction9(in : ShaderFunction9In) -> ShaderFunction9Out {
//         ps_v0 = in.loc0;
//         ShaderFunction9_body();
//         var out : ShaderFunction9Out;
//         out.loc0 = ps_oC0;
//         return out;
//     }
//
// The entry point KEEPS ITS SPIR-V NAME (`MOJOSHADER_parseData::mainfn`, e.g. `ShaderFunction9`),
// because that is the string the renderer already passes as the pipeline's entry point. The body
// takes the `_body` suffix, not the entry point.
//
// Bind groups, binding numbers and vertex input locations are reproduced exactly as the SPIR-V
// carried them, so the renderer's bind-group layouts and vertex layouts are unchanged between the
// two routes. That is what makes the native route a usable oracle for this one: the same effect,
// the same draw and the same expected pixels, with only the module representation swapped.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    /** @brief Outcome of one SPIR-V to WGSL translation. */
    struct SpirvToWgslResult
    {
        /** @brief The WGSL source, or empty when @ref error is set. */
        std::string wgsl;
        /** @brief Entry point name, equal to the name the SPIR-V's OpEntryPoint carried. */
        std::string entryPoint;
        /** @brief Empty on success; otherwise why the module could not be translated. */
        std::string error;
    };

    /**
     * @brief Translates one MojoShader-emitted SPIR-V module into equivalent WGSL.
     *
     * The accepted subset is the one this repository's compiled-effect pipeline can emit; see this
     * file's header comment for the measurement that fixes it. Anything outside it is refused with
     * a message naming the construct, never approximated.
     *
     * @param words Module words, starting at the magic number. Must already have been through
     *        SplitCombinedImageSamplers(): WGSL has no combined image sampler type.
     * @param wordCount Number of words in @p words.
     * @return The WGSL and its entry point name, or an error describing what was refused.
     */
    [[nodiscard]] SpirvToWgslResult TranslateSpirvToWgsl(const std::uint32_t* words,
                                                         std::size_t wordCount);
}
