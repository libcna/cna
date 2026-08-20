// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// plans/plan_metal.md METAL-34: MetalRenderPipelineState cache key + hash, extracted from
// MetalRenderer.mm into its own plain-C++ header (no Objective-C, no Apple framework
// dependency) so its hash/equality logic can be exercised by a normal GoogleTest binary on any
// platform -- the one piece of the Metal renderer's pipeline cache genuinely build-verifiable
// without an Apple toolchain, since METAL-1..255's own MSL/Objective-C++ code cannot compile here.
// MetalRenderer.mm includes this header rather than defining these types inline; logic is
// unchanged from that original inline definition.
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
    /** @brief Built-in Metal shader and vertex-layout variants. */
    enum class MetalPipelineKind : uint8_t
    {
        /** @brief Position-and-color input with a 16-byte stride. */
        Colored16,
        /** @brief Position-and-texture input with a 20-byte stride. */
        Textured20,
        /** @brief Position, color, and texture input with a 24-byte stride. */
        ColorTex24,
        /** @brief Per-pixel-lit normal-and-texture input with a 32-byte stride. */
        LitTex32,
        /** @brief Per-vertex-lit normal-and-texture input with a 32-byte stride. */
        LitTex32VertexLit,
        /** @brief Dual-texture input with a 20-byte stride. */
        DualTex20,
        /** @brief Colored dual-texture input with a 24-byte stride. */
        DualTex24Colored,
        /** @brief Environment-map input with a 32-byte stride. */
        EnvMap32,
        /** @brief Per-pixel-lit skinned input with a 52-byte stride. */
        Skinned52,
        /** @brief Colored per-pixel-lit skinned input with a 56-byte stride. */
        Skinned56,
        /** @brief Per-vertex-lit skinned input with a 52-byte stride. */
        Skinned52VertexLit,
        /** @brief Colored per-vertex-lit skinned input with a 56-byte stride. */
        Skinned56VertexLit,
        /** @brief Physically based unskinned input with a 48-byte stride. */
        Pbr48,
        /** @brief Physically based skinned input with a 68-byte stride. */
        SkinnedPbr68,
        /** @brief Built-in two-dimensional SpriteBatch pipeline. */
        Sprite2D
    };

    // Metal bakes blend factors/operations into MTLRenderPipelineState (unlike depth/stencil/
    // cull/fill, which are genuine dynamic encoder state already handled elsewhere in the renderer)
    // -- so a real per-BlendState pipeline cache needs blend as part of its key. Defaults below
    // match Blend::One=0/Blend::Zero=1/BlendFunction::Add=0 for both channels, i.e. BlendState.
    // Opaque's own real values -- the correct answer for "no ApplyBlendState call happened yet"
    // (matches GraphicsDevice's own real XNA default BlendState).
    /** @brief Blend state baked into a Metal render-pipeline cache key. */
    struct MetalBlendKey
    {
        uint8_t colorSrc=0, colorDst=1, alphaSrc=0, alphaDst=1, colorFunc=0, alphaFunc=0;
        bool enabled=false;
        /**
         * @brief Compares every baked blend-state field.
         *
         * @param other Blend key to compare.
         * @return True when both keys describe identical blend state.
         */
        [[nodiscard]] bool operator==(const MetalBlendKey& other) const
        {
            return colorSrc==other.colorSrc && colorDst==other.colorDst &&
                   alphaSrc==other.alphaSrc && alphaDst==other.alphaDst &&
                   colorFunc==other.colorFunc && alphaFunc==other.alphaFunc &&
                   enabled==other.enabled;
        }
    };

    /**
     * @brief Returns the complete last-writer state installed by SetBlendEnabled.
     *
     * Enabling selects CNA's established straight-alpha factors; disabling selects opaque. A later
     * ApplyBlendState replaces this complete key rather than being AND-latched by an older toggle.
     *
     * @param enabled True for straight-alpha blending; false for opaque blending.
     * @return Complete blend key to install as the current state.
     */
    [[nodiscard]] constexpr MetalBlendKey MetalBlendKeyForSetBlendEnabled(bool enabled) noexcept
    {
        return enabled ? MetalBlendKey{4, 5, 4, 5, 0, 0, true} : MetalBlendKey{};
    }
    // plans/plan_metal.md METAL-33: no eviction is implemented, and none is needed for a v1 renderer --
    // the key space is small and effectively bounded, the same reasoning EasyGLRenderer's
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
    // practice, an LRU eviction policy would be the right CNAEXT follow-up then, not something to
    // design speculatively now.
    // plans/plan_metal.md METAL-113: `colorAttachmentCount` joins `kind`/`blend` in the key once MRT
    // (METAL-112) lands -- a render pipeline's `MTLRenderPipelineDescriptor.colorAttachments[i]`
    // array must be declared for exactly as many simultaneous attachments as the active render
    // pass actually binds (1 for the ordinary backbuffer/single-RT/cube-face case, up to Metal's
    // own 8-attachment hardware limit for real `SetRenderTargets()` MRT), or `newRenderPipeline
    // StateWithDescriptor:` raises a genuine validation error, not just a style mismatch. Every
    // attachment always shares the same `MTLPixelFormatBGRA8Unorm` format (confirmed by
    // `MetalRenderTargetRenderer`'s own hardcoded choice, see `plans/plan_metal.md` narrative item 77),
    // so only the *count* needs to vary, not a per-slot format list -- matching
    // `VulkanRenderer`'s own identical `colorAttachmentCount`-folded-into-the-pipeline-key
    // precedent (its `PickRTPipelineRenderPass`/pipeline-key helpers), confirmed by reading it
    // directly. Defaults to `1` so every pre-MRT call site (an aggregate-initializing
    // `PipelineCacheKey key{kind, currentBlend};` with only 2 explicit fields) is unaffected.
    // plans/plan_metal.md METAL-104/105: `sampleCount` (1/2/4/8) joins `colorAttachmentCount` once MSAA
    // lands -- a render pipeline's `MTLRenderPipelineDescriptor.sampleCount` must match the active
    // render pass's own sample count, or pipeline creation is a genuine Metal API validation
    // error, exactly the same class of constraint `colorAttachmentCount` above already documents,
    // now for the orthogonal MSAA axis instead of the MRT one. Defaults to `1` (no MSAA) so every
    // pre-MSAA call site is unaffected.
    /** @brief Complete identity of one cached Metal render pipeline. */
    struct MetalPipelineCacheKey
    {
        MetalPipelineKind kind; MetalBlendKey blend; uint8_t colorAttachmentCount = 1; uint8_t sampleCount = 1;
        /**
         * @brief Compares every field that affects Metal render-pipeline compatibility.
         *
         * @param other Pipeline key to compare.
         * @return True when both keys identify the same pipeline state.
         */
        [[nodiscard]] bool operator==(const MetalPipelineCacheKey& other) const
        {
            return kind==other.kind && blend==other.blend &&
                   colorAttachmentCount==other.colorAttachmentCount &&
                   sampleCount==other.sampleCount;
        }
    };
    /** @brief Hash functor for Metal render-pipeline cache keys. */
    struct MetalPipelineCacheKeyHash
    {
        /**
         * @brief Hashes every field in a Metal render-pipeline cache key.
         *
         * @param key Pipeline key to hash.
         * @return Hash value suitable for an unordered associative container.
         */
        [[nodiscard]] std::size_t operator()(const MetalPipelineCacheKey& key) const
        {
            uint64_t h = (uint64_t)key.kind
                | ((uint64_t)key.blend.colorSrc  << 8)
                | ((uint64_t)key.blend.colorDst  << 16)
                | ((uint64_t)key.blend.alphaSrc  << 24)
                | ((uint64_t)key.blend.alphaDst  << 32)
                | ((uint64_t)key.blend.colorFunc << 40)
                | ((uint64_t)key.blend.alphaFunc << 48)
                | ((uint64_t)(key.blend.enabled ? 1 : 0) << 56)
                | ((uint64_t)key.colorAttachmentCount << 57);
            // plans/plan_metal.md METAL-104: sampleCount combined via a second, independent hash (the
            // standard hash_combine formula) rather than packed into the same 64-bit word as
            // everything above -- colorAttachmentCount already occupies bits 57-60 (4 bits, enough
            // for its own 1-8 range), leaving only 3 bits free, one short of sampleCount's own 1-8
            // range -- XOR-combining avoids reshuffling every existing field's bit offset (needless
            // churn/regression risk for an already-working scheme) just to make room.
            std::size_t hh = std::hash<uint64_t>()(h);
            hh ^= std::hash<uint8_t>()(key.sampleCount) + 0x9e3779b97f4a7c15ULL + (hh<<6) + (hh>>2);
            return hh;
        }
    };
}
