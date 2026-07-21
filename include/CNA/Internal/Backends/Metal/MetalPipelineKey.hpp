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
    // plan_metal.md METAL-33: no eviction is implemented, and none is needed for a v1 backend --
    // the key space is small and effectively bounded, the same reasoning EasyGLGraphicsBackend's
    // own Prog3D relies on implicitly (a fixed struct field per shader variant instead of a hashed
    // cache at all), just made explicit here since MetalPipelineCacheKey genuinely is a dynamic
    // cache. `MetalPipelineKind` is a compile-time-fixed 15-value enum (this file's own
    // declaration above); `MetalBlendKey`'s own theoretical space is much larger (each of 6 blend
    // fields plus `enabled`), but in practice a real game only ever calls `ApplyBlendState()` with
    // a handful of distinct `BlendState` combinations -- XNA's own 4 built-in presets
    // (`Opaque`/`AlphaBlend`/`Additive`/`NonPremultiplied`) cover the overwhelming majority of real
    // usage, plus whatever small number of custom `BlendState`s a specific game explicitly
    // constructs. Even a pathologically blend-heavy game hitting a few dozen distinct
    // `BlendState`s still caps this cache at kind-count × blend-count = a few hundred entries at
    // most, each holding one lightweight `id<MTLRenderPipelineState>` reference -- not a real
    // memory or lookup-cost concern. If a genuinely pathological case (e.g. a game that
    // procedurally constructs thousands of distinct one-off `BlendState`s) is ever observed in
    // practice, an LRU eviction policy would be the right NOXNA follow-up then, not something to
    // design speculatively now.
    // plan_metal.md METAL-113: `colorAttachmentCount` joins `kind`/`blend` in the key once MRT
    // (METAL-112) lands -- a render pipeline's `MTLRenderPipelineDescriptor.colorAttachments[i]`
    // array must be declared for exactly as many simultaneous attachments as the active render
    // pass actually binds (1 for the ordinary backbuffer/single-RT/cube-face case, up to Metal's
    // own 8-attachment hardware limit for real `SetRenderTargets()` MRT), or `newRenderPipeline
    // StateWithDescriptor:` raises a genuine validation error, not just a style mismatch. Every
    // attachment always shares the same `MTLPixelFormatBGRA8Unorm` format (confirmed by
    // `MetalRenderTargetBackend`'s own hardcoded choice, see `plan_metal.md` narrative item 77),
    // so only the *count* needs to vary, not a per-slot format list -- matching
    // `VulkanGraphicsBackend`'s own identical `colorAttachmentCount`-folded-into-the-pipeline-key
    // precedent (its `PickRTPipelineRenderPass`/pipeline-key helpers), confirmed by reading it
    // directly. Defaults to `1` so every pre-MRT call site (an aggregate-initializing
    // `PipelineCacheKey key{kind, currentBlend};` with only 2 explicit fields) is unaffected.
    // plan_metal.md METAL-104/105: `sampleCount` (1/2/4/8) joins `colorAttachmentCount` once MSAA
    // lands -- a render pipeline's `MTLRenderPipelineDescriptor.sampleCount` must match the active
    // render pass's own sample count, or pipeline creation is a genuine Metal API validation
    // error, exactly the same class of constraint `colorAttachmentCount` above already documents,
    // now for the orthogonal MSAA axis instead of the MRT one. Defaults to `1` (no MSAA) so every
    // pre-MSAA call site is unaffected.
    struct MetalPipelineCacheKey
    {
        MetalPipelineKind kind; MetalBlendKey blend; uint8_t colorAttachmentCount = 1; uint8_t sampleCount = 1;
        bool operator==(const MetalPipelineCacheKey& o) const
        {
            return kind==o.kind && blend==o.blend && colorAttachmentCount==o.colorAttachmentCount &&
                   sampleCount==o.sampleCount;
        }
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
                | ((uint64_t)(k.blend.enabled ? 1 : 0) << 56)
                | ((uint64_t)k.colorAttachmentCount << 57);
            // plan_metal.md METAL-104: sampleCount combined via a second, independent hash (the
            // standard hash_combine formula) rather than packed into the same 64-bit word as
            // everything above -- colorAttachmentCount already occupies bits 57-60 (4 bits, enough
            // for its own 1-8 range), leaving only 3 bits free, one short of sampleCount's own 1-8
            // range -- XOR-combining avoids reshuffling every existing field's bit offset (needless
            // churn/regression risk for an already-working scheme) just to make room.
            std::size_t hh = std::hash<uint64_t>()(h);
            hh ^= std::hash<uint8_t>()(k.sampleCount) + 0x9e3779b97f4a7c15ULL + (hh<<6) + (hh>>2);
            return hh;
        }
    };
}
