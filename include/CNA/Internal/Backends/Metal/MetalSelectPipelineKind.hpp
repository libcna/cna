#pragma once

#include "CNA/Internal/Backends/Metal/MetalPipelineKey.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <cstddef>
#include <stdexcept>

// plan_metal.md METAL-34-style extraction: this is the real shader-variant dispatch logic --
// arguably the single most safety-critical function in the whole backend, since it decides which
// concrete MSL shader pair every 3D draw call actually uses. It only reads GpuDrawParams (plain
// C++, zero Objective-C dependency) and a stride, so unlike drawMetal3D() itself (which also issues
// real Metal API calls) this dispatch decision can be genuinely unit-tested on any platform without
// an Apple toolchain. MetalGraphicsBackend.mm includes this header instead of defining the function
// inline; logic (including precedence order and every error message) is unchanged from the original
// inline definition.
namespace CNA::Internal::Backends::Metal
{
    // plan_metal.md Phase 2 (simplified for a first, hardware-unverified pass -- a fully generic
    // VertexDeclaration-driven descriptor builder, METAL-27, stays open; this is a fixed-variant
    // enum, one entry per concrete shader+vertex-layout combination this backend actually emits,
    // exactly mirroring the "one Prog3D per Ensure*Program()" shape EasyGLGraphicsBackend already
    // uses -- lower risk to get right without a compiler than inventing a hashed-VertexElement-list
    // key blind).
    // plan_metal.md METAL-38: `LitTex32` replaces the earlier plain-unlit `NormalTex32` entry --
    // confirmed by reading EasyGLGraphicsBackend::SelectProgram()'s real `switch(stride)` that
    // stride 32 (VertexPositionNormalTexture) *always* selects a lit shader, never an unlit one,
    // even when `lightingEnabled=false` (BindDrawParams() sets ambient=(1,1,1) and zeroes every
    // light's diffuse/specular contribution in that case, which makes the lit formula degenerate
    // to the exact same "just DiffuseColor * texture" result an unlit shader would produce --
    // verified by reading that exact branch, not assumed).
    //
    // Precedence order, matching EasyGLGraphicsBackend::SelectProgram()'s own real precedence
    // exactly: pbr (+skinned) > skinned > envMapping > dualTexture > textured > colored.
    inline CNA::Internal::Backends::Metal::MetalPipelineKind SelectMetalPipelineKind(
        std::size_t stride, const CNA::Internal::Backends::GpuDrawParams* params)
    {
        using PipelineKind = MetalPipelineKind;
        const bool textured = params && params->texture0;
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
            // plan_metal.md METAL-76: same real XNA precedence as METAL-39's BasicEffect case
            // (matching EasyGLGraphicsBackend::SelectProgram()'s own identical skinned branch) --
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
            if (!textured) throw std::runtime_error("Metal: DualTextureEffect requires Texture to be set");
            if (stride == 24) return PipelineKind::DualTex24Colored;
            if (stride == 20) return PipelineKind::DualTex20;
            throw std::runtime_error("Metal: DualTextureEffect requires stride 20 or 24");
        }
        if (textured) {
            switch (stride) {
                case 20: return PipelineKind::Textured20;
                case 24: return PipelineKind::ColorTex24;
                case 32:
                    // plan_metal.md METAL-39: real XNA BasicEffect precedence (matches
                    // EasyGLGraphicsBackend::SelectProgram()'s own stride-32 case exactly) -- with
                    // lighting disabled, both shaders degenerate to the identical trivial
                    // ambient=(1,1,1) case, so the per-pixel-lit pipeline stays selected there rather
                    // than forcing an unnecessary extra PipelineKind/pipeline-cache entry.
                    if (params && params->lightingEnabled && !params->preferPerPixelLighting)
                        return PipelineKind::LitTex32VertexLit;
                    return PipelineKind::LitTex32;
                default: throw std::runtime_error("Metal: textured 3D requires stride 20, 24, or 32 until generic VertexDeclaration pipeline cache is implemented");
            }
        }
        if (stride != 16) throw std::runtime_error("Metal: colored 3D currently requires VertexPositionColor stride 16");
        return PipelineKind::Colored16;
    }
}
