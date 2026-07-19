#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// plan_metal.md METAL-34: MetalRenderPipelineState cache key + hash, extracted from
// MetalGraphicsBackend.mm into its own plain-C++ header (no Objective-C, no Apple framework
// dependency) so its hash/equality logic can be exercised by a normal GoogleTest binary on any
// platform -- the one piece of the Metal backend's pipeline cache genuinely build-verifiable
// without an Apple toolchain, since METAL-1..255's own MSL/Objective-C++ code cannot compile here.
// MetalGraphicsBackend.mm includes this header rather than defining these types inline; logic is
// unchanged from that original inline definition.
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
    enum class MetalPipelineKind : uint8_t
    {
        Colored16, Textured20, ColorTex24, LitTex32, LitTex32VertexLit, DualTex20, DualTex24Colored, EnvMap32,
        Skinned52, Skinned56, Skinned52VertexLit, Skinned56VertexLit, Pbr48, SkinnedPbr68, Sprite2D
    };

    // Metal bakes blend factors/operations into MTLRenderPipelineState (unlike depth/stencil/
    // cull/fill, which are genuine dynamic encoder state already handled elsewhere in the backend)
    // -- so a real per-BlendState pipeline cache needs blend as part of its key. Defaults below
    // match Blend::One=0/Blend::Zero=1/BlendFunction::Add=0 for both channels, i.e. BlendState.
    // Opaque's own real values -- the correct answer for "no ApplyBlendState call happened yet"
    // (matches GraphicsDevice's own real XNA default BlendState).
    struct MetalBlendKey
    {
        uint8_t colorSrc=0, colorDst=1, alphaSrc=0, alphaDst=1, colorFunc=0, alphaFunc=0;
        bool enabled=false;
        bool operator==(const MetalBlendKey& o) const
        {
            return colorSrc==o.colorSrc && colorDst==o.colorDst && alphaSrc==o.alphaSrc &&
                   alphaDst==o.alphaDst && colorFunc==o.colorFunc && alphaFunc==o.alphaFunc &&
                   enabled==o.enabled;
        }
    };
    struct MetalPipelineCacheKey
    {
        MetalPipelineKind kind; MetalBlendKey blend;
        bool operator==(const MetalPipelineCacheKey& o) const { return kind==o.kind && blend==o.blend; }
    };
    struct MetalPipelineCacheKeyHash
    {
        std::size_t operator()(const MetalPipelineCacheKey& k) const
        {
            uint64_t h = (uint64_t)k.kind
                | ((uint64_t)k.blend.colorSrc  << 8)
                | ((uint64_t)k.blend.colorDst  << 16)
                | ((uint64_t)k.blend.alphaSrc  << 24)
                | ((uint64_t)k.blend.alphaDst  << 32)
                | ((uint64_t)k.blend.colorFunc << 40)
                | ((uint64_t)k.blend.alphaFunc << 48)
                | ((uint64_t)(k.blend.enabled ? 1 : 0) << 56);
            return std::hash<uint64_t>()(h);
        }
    };
}
