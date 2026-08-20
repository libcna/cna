// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Metal/MetalPipelineKey.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <cstddef>
#include <stdexcept>

// plans/plan_metal.md METAL-34-style extraction: this is the real shader-variant dispatch logic --
// arguably the single most safety-critical function in the whole renderer, since it decides which
// concrete MSL shader pair every 3D draw call actually uses. It only reads GpuDrawParams (plain
// C++, zero Objective-C dependency) and a stride, so unlike drawMetal3D() itself (which also issues
// real Metal API calls) this dispatch decision can be genuinely unit-tested on any platform without
// an Apple toolchain. MetalRenderer.mm includes this header instead of defining the function
// inline, keeping precedence, conservative stride rejection, and texture-independent stock dispatch
// in one portable source of truth.
namespace CNA::Internal::Renderers::Metal
{
    // plans/plan_metal.md Phase 2 (simplified for a first, hardware-unverified pass -- a fully generic
    // VertexDeclaration-driven descriptor builder, METAL-27, stays open; this is a fixed-variant
    // enum, one entry per concrete shader+vertex-layout combination this renderer actually emits,
    // exactly mirroring the "one Prog3D per Ensure*Program()" shape EasyGLRenderer already
    // uses -- lower risk to get right without a compiler than inventing a hashed-VertexElement-list
    // key blind).
    // plans/plan_metal.md METAL-38: `LitTex32` replaces the earlier plain-unlit `NormalTex32` entry --
    // confirmed by reading EasyGLRenderer::SelectProgram()'s real `switch(stride)` that
    // stride 32 (VertexPositionNormalTexture) *always* selects a lit shader, never an unlit one,
    // even when `lightingEnabled=false` (BindDrawParams() sets ambient=(1,1,1) and zeroes every
    // light's diffuse/specular contribution in that case, which makes the lit formula degenerate
    // to the exact same "just DiffuseColor * texture" result an unlit shader would produce --
    // verified by reading that exact branch, not assumed).
    //
    // Precedence order, matching EasyGLRenderer::SelectProgram()'s own real precedence
    // exactly: pbr (+skinned) > skinned > envMapping > dualTexture > textured > colored.
    /**
     * @brief Chooses the built-in Metal pipeline variant for one normalized draw.
     *
     * @param stride Combined vertex stride in bytes.
     * @param params Optional normalized draw parameters; null selects legacy colored input.
     * @return The required built-in Metal pipeline variant.
     */
    [[nodiscard]] inline CNA::Internal::Renderers::Metal::MetalPipelineKind SelectMetalPipelineKind(
        std::size_t stride, const CNA::Internal::Renderers::GpuDrawParams* params)
    {
        using PipelineKind = MetalPipelineKind;
        const bool pbr = params && params->pbr;
        const bool skinned = params && params->skinned;
        const bool envMapping = params && params->envMapping;
        const bool dual = params && params->dualTexture;
        if (pbr) {
            if (skinned) {
                if (stride != 68) throw std::runtime_error("Metal: SkinnedPbrEffect requires stride 68 (position+normal+tangent+uv+boneWeights+boneIndices)");
                return PipelineKind::SkinnedPbr68;
            }
            if (stride != 48) throw std::runtime_error("Metal: PbrEffect requires stride 48 (position+normal+tangent+uv)");
            return PipelineKind::Pbr48;
        }
        if (skinned) {
            // plans/plan_metal.md METAL-76: same real XNA precedence as METAL-39's BasicEffect case
            // (matching EasyGLRenderer::SelectProgram()'s own identical skinned branch) --
            // per-vertex-lit only when lighting is actually on and per-pixel wasn't explicitly
            // requested; with lighting disabled both shaders degenerate identically, so the existing
            // per-pixel pipeline stays selected there too.
            const bool vertexLit = params && params->lightingEnabled && !params->preferPerPixelLighting;
            if (stride == 56) return vertexLit ? PipelineKind::Skinned56VertexLit : PipelineKind::Skinned56;
            if (stride == 52) return vertexLit ? PipelineKind::Skinned52VertexLit : PipelineKind::Skinned52;
            throw std::runtime_error("Metal: SkinnedEffect requires stride 52 or 56");
        }
        if (envMapping) {
            if (stride != 32) throw std::runtime_error("Metal: EnvironmentMapEffect requires VertexPositionNormalTexture (stride 32)");
            return PipelineKind::EnvMap32;
        }
        if (dual) {
            if (stride == 24) return PipelineKind::DualTex24Colored;
            if (stride == 20) return PipelineKind::DualTex20;
            throw std::runtime_error("Metal: DualTextureEffect requires stride 20 or 24");
        }
        // METAL-271: pipeline shape comes from the captured effect flags and canonical vertex
        // stride, never from whether a texture pointer happens to be null. Every stock 2D sample
        // has an owned white fallback, so an untextured BasicEffect/AlphaTestEffect remains a
        // valid draw instead of falling through to the unrelated Colored16 route.
        if (params) {
            switch (stride) {
                case 20: return PipelineKind::Textured20;
                case 24: return PipelineKind::ColorTex24;
                case 32:
                    // plans/plan_metal.md METAL-39: real XNA BasicEffect precedence (matches
                    // EasyGLRenderer::SelectProgram()'s own stride-32 case exactly) -- with
                    // lighting disabled, both shaders degenerate to the identical trivial
                    // ambient=(1,1,1) case, so the per-pixel-lit pipeline stays selected there rather
                    // than forcing an unnecessary extra PipelineKind/pipeline-cache entry.
                    if (params && params->lightingEnabled && !params->preferPerPixelLighting)
                        return PipelineKind::LitTex32VertexLit;
                    return PipelineKind::LitTex32;
                case 16: return PipelineKind::Colored16;
                default: throw std::runtime_error("Metal: stock 3D requires stride 16, 20, 24, or 32 until generic VertexDeclaration pipeline cache is implemented");
            }
        }
        if (stride != 16) throw std::runtime_error("Metal: colored 3D currently requires VertexPositionColor stride 16");
        return PipelineKind::Colored16;
    }
}
