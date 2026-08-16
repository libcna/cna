// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SdlGpuCompiledEffectVertexLayout.hpp
 * @brief plan_fx.md FX-071: the generic `VertexDeclaration`-to-`SDL_GPUVertexAttribute` builder a
 * compiled-effect draw needs.
 *
 * Every one of this renderer's eight built-in draw routes selects a fixed, hand-written
 * `SDL_GPUVertexAttribute` set for its own fixed stride (see e.g. `GetOrCreatePipelineColored3D`).
 * A compiled XNA effect's vertex shader is arbitrary, so its pipeline needs an attribute set built
 * from two things neither of those routes has to reconcile: the caller's own `VertexDeclaration`
 * (element usage/index/offset/format) and the applied pass's own vertex shader reflection (which
 * usage/index pairs it actually declares as inputs, and at which location).
 *
 * SDL_GPU's SPIR-V vertex inputs are located by MojoShader in the same order as
 * `MOJOSHADER_parseData::attributes`: `MOJOSHADER_sdlGetVertexAttribLocation` (mojoshader_sdlgpu.c)
 * returns the array index of the matching entry, not a value queried from the compiled shader
 * module. This builder iterates that same array directly rather than calling that accessor once per
 * declared element, since the array index it would return back is exactly the loop index here.
 */

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "mojoshader.h"

#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <SDL3/SDL_gpu.h>

#include <vector>

namespace CNA::Internal::Renderers::SdlGpu
{
    /**
     * @brief Maps one XNA vertex element format to its SDL_GPU vertex attribute format.
     *
     * `VertexElementFormat::Color` maps to a normalized four-byte format, matching every existing
     * hardcoded stock pipeline's own convention for XNA `Color`-typed elements (e.g.
     * `GetOrCreatePipelineColored3D`); `Byte4` maps to the unnormalized equivalent.
     *
     * @param format The XNA vertex element format.
     * @return The equivalent SDL_GPU format.
     * @throws std::invalid_argument if @p format is not one of the twelve XNA formats.
     */
    [[nodiscard]] SDL_GPUVertexElementFormat ToSdlGpuVertexElementFormat(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format);

    /**
     * @brief Maps one XNA vertex element usage to MojoShader's attribute usage.
     *
     * XNA's `VertexElementUsage` and MojoShader's `MOJOSHADER_usage` both mirror Direct3D 9 vertex
     * declaration usages and correspond one-to-one; MojoShader additionally declares
     * `MOJOSHADER_USAGE_POSITIONT` (transformed position), which XNA's enumeration has no member
     * for and a `VertexDeclaration` therefore never produces.
     *
     * @param usage The XNA vertex element usage.
     * @return The equivalent MojoShader usage.
     * @throws std::invalid_argument if @p usage is not one of the thirteen XNA usages.
     */
    [[nodiscard]] MOJOSHADER_usage ToMojoShaderUsage(
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage);

    /**
     * @brief Builds the SDL_GPU vertex attribute set a compiled effect's applied vertex shader needs.
     *
     * Walks @p vertexParseData's own reflected attribute list -- not @p declaredElements -- because
     * that list is exactly, and only, what the shader consumes: a declared element the shader does
     * not read needs no attribute (an unused declared element is not an error, matching real XNA
     * behavior), while every attribute the shader DOES declare must resolve to a declared element or
     * the draw cannot supply that input at all.
     *
     * @param vertexParseData The applied pass's vertex shader reflection
     *        (`MOJOSHADER_sdlGetShaderParseData` on the bound vertex shader data).
     * @param declaredElements The caller's `VertexDeclaration` elements, in declaration order.
     * @param bufferSlot The SDL_GPU vertex buffer slot every attribute binds to. This builder
     *        supports one vertex stream; multi-stream compiled-effect draws are not yet implemented.
     * @return One `SDL_GPUVertexAttribute` per shader attribute, `location` set to that attribute's
     *         index in @p vertexParseData (matching `MOJOSHADER_sdlGetVertexAttribLocation`'s own
     *         convention), in that same order.
     * @throws System::NotSupportedException if a shader attribute has no matching declared element
     *         (same usage and usage index), naming the missing input.
     */
    [[nodiscard]] std::vector<SDL_GPUVertexAttribute> BuildCompiledEffectVertexAttributes(
        const MOJOSHADER_parseData& vertexParseData,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
        Uint32 bufferSlot);
}

#endif  // CNA_SDL_GPU_COMPILED_EFFECTS
