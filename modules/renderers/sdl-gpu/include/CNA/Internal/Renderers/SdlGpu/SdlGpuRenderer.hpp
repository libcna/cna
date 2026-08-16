// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
#include "mojoshader.h"
#endif
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::SdlGpu
{
    class SdlGpuRenderer;
    class SdlGpuRenderTargetRenderer;
    class SdlGpuRenderTargetCubeRenderer;
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
    class SdlGpuCompiledEffect;
#endif

    /**
     * @brief Scoped, renderer-instance-local failure points used by the SDL_GPU lifetime
     * regression. CNAEXT.
     *
     * These hooks deliberately sit above SDL rather than replacing it: successful stages still
     * acquire real SDL_GPU resources, and a selected later stage throws before making its native
     * call. Production construction uses None and pays only one predictable branch per stage.
     */
    enum class SdlGpuFailurePointEXT : std::uint8_t
    {
        None,
        DeviceCreation,
        WindowClaim,
        SwapchainSetup,
        DepthStencilFormatQuery,
        SpriteVertexShaderCreation,
        SpriteFragmentShaderCreation,
        ColoredVertexShaderCreation,
        ColoredFragmentShaderCreation,
        TexturedVertexShaderCreation,
        ColoredTexturedVertexShaderCreation,
        TexturedFragmentShaderCreation,
        LitTexturedVertexShaderCreation,
        LitTexturedFragmentShaderCreation,
        AlphaTestVertexShaderCreation,
        AlphaTestColoredVertexShaderCreation,
        AlphaTestFragmentShaderCreation,
        DualTextureVertexShaderCreation,
        DualTextureColoredVertexShaderCreation,
        DualTextureFragmentShaderCreation,
        EnvMapVertexShaderCreation,
        EnvMapFragmentShaderCreation,
        SkinnedVertexShaderCreation,
        SkinnedColoredVertexShaderCreation,
        SkinnedColoredFragmentShaderCreation,
        PbrVertexShaderCreation,
        PbrSkinnedVertexShaderCreation,
        PbrFragmentShaderCreation,
        WindowMetricsInitialization,
        RendererRegistration,
        AfterRendererRegistration,
        FrameCommandBufferAcquisition,
        GraphicsPipelineCreation,
        SamplerCreation,
        DefaultWhiteTextureCreation,
        DefaultFlatNormalTextureCreation
    };

    /** @brief Resource categories reported by SdlGpuTestHooksEXT. CNAEXT. */
    enum class SdlGpuResourceKindEXT : std::uint8_t
    {
        Device,
        WindowClaim,
        Shader,
        FrameCommandBuffer,
        GraphicsPipeline,
        Sampler,
        DefaultTexture
    };

    /** @brief Acquisition/release edge reported by SdlGpuTestHooksEXT. CNAEXT. */
    enum class SdlGpuResourceEventEXT : std::uint8_t
    {
        Acquired,
        Released
    };

    /**
     * @brief Optional per-instance test injection and resource-lifetime observation. CNAEXT.
     *
     * `context` must outlive the renderer when `resourceEvent` is supplied. The callback is
     * noexcept so cleanup can preserve the exception that triggered it.
     */
    struct SdlGpuTestHooksEXT
    {
        SdlGpuFailurePointEXT failAt = SdlGpuFailurePointEXT::None;
        void* context = nullptr;
        void (*resourceEvent)(void* context, SdlGpuResourceKindEXT resource,
                              SdlGpuResourceEventEXT event) noexcept = nullptr;
    };

    /**
     * @brief The one bindable form of a sampled texture in this renderer (REMED-GFX-152). CNAEXT.
     *
     * A public `Texture2D` and a public `RenderTarget2D` arrive at every draw as the SAME static
     * type, `const ITextureRenderer*`, but their concrete renderers — `SdlGpuTextureRenderer` and
     * `SdlGpuRenderTargetRenderer` — are unrelated SIBLINGS, both merely deriving from
     * `ITextureRenderer` (and the former is `final`, so a render target can never be one). Every
     * binding route therefore has to ANSWER a question, not assume an answer, and this value object
     * is that answer: the native, sampleable, already-resolved handle, plus a reference that keeps
     * whatever owns it alive for exactly as long as the command holding it can still be replayed.
     *
     * `keepAlive` matters because this renderer is a whole-frame-deferred recorder: a draw queued now
     * is issued at `EnsureFrameRendered()`, and the public `Texture2D`/`RenderTarget2D` it sampled
     * may be a short-lived local that has already been destroyed by then. Holding the owner's shared
     * GPU state (never the wrapper object, which is not shared) means the native handle stays valid
     * and its release stays deferred past the submit that consumes it — the same contract
     * `SdlGpuRenderTarget2DState` already gave render targets, now extended to ordinary textures so
     * a command can never outlive the resource it binds.
     *
     * Copying one costs a refcount increment; it allocates nothing.
     */
    struct SdlGpuSampledTextureEXT
    {
        /** @brief The sampleable native texture — for MSAA render targets, the RESOLVED one. */
        SDL_GPUTexture* texture = nullptr;
        /** @brief Keeps the resource owning @ref texture alive while a queued command can bind it. */
        std::shared_ptr<const void> keepAlive;

        /** @brief Whether a texture was actually resolved (an absent optional slot yields false). */
        [[nodiscard]] explicit operator bool() const noexcept { return texture != nullptr; }
    };

    // The GPU handle of an ordinary uploaded texture (Texture2D, TextureCube) lives in this
    // separately-owned struct rather than on the wrapper, for the same reason
    // SdlGpuRenderTarget2DState exists (see its own doc comment): draws are replayed at Present,
    // long after a short-lived public texture may have been destroyed, so the native handle has to
    // outlive the wrapper and its release has to be deferred until the command buffer that samples
    // it has been submitted (REMED-GFX-152). Render targets keep their own richer state struct;
    // this is the minimal one for resources that are nothing but a sampleable texture.
    struct SdlGpuSampledTextureState
    {
        SdlGpuRenderer* owner = nullptr;
        SDL_GPUTexture* texture = nullptr;

        ~SdlGpuSampledTextureState();
    };

    /**
     * @brief `SDL_gpu`-backed `Texture2D`. Plain 2D, `SAMPLER` usage only, with its declared chain.
     *
     * REMED-GFX-176: one native `SDL_GPUTexture` holding **every level `ImageData::mipLevels`
     * declares**, not the single level this class used to hardcode. Allocation is all construction
     * does: a level's content arrives only through `UpdatePixels` (level 0) or `UpdatePixelsLevel`
     * (any level), exactly as the caller supplies it. Nothing here derives, resamples or generates
     * a level, so a `mipMap=true` texture whose upper levels were never written keeps them as the
     * undefined-but-allocated storage the API promised rather than acquiring invented content.
     * A `mipMap=false` texture is a one-level resource and its upload path is unchanged.
     */
    class SdlGpuTextureRenderer final : public ITextureRenderer
    {
    public:
        SdlGpuTextureRenderer(SdlGpuRenderer& owner, const ImageData& data);
        ~SdlGpuTextureRenderer() override;

        SdlGpuTextureRenderer(const SdlGpuTextureRenderer&) = delete;
        SdlGpuTextureRenderer& operator=(const SdlGpuTextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// REMED-GFX-176: uploads exactly @p level, sized by that level's own dimensions. An
        /// out-of-range level or a null source is ignored, matching VulkanTextureRenderer's
        /// established convention for this void-returning interface method.
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /** @brief Returns the underlying `SDL_GPUTexture`. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* Texture() const { return state_->texture; }
        /**
         * @brief Returns the number of mip levels SDL really allocated. CNAEXT — REMED-GFX-176.
         *
         * @return The native `num_levels` this texture was created with; never below one.
         */
        CNAEXT [[nodiscard]] int LevelCountEXT() const { return levelCount_; }
        /**
         * @brief Returns this texture as a bindable, lifetime-safe sampled resource. CNAEXT.
         *
         * @return The native handle paired with the shared state that keeps it alive.
         */
        CNAEXT [[nodiscard]] SdlGpuSampledTextureEXT Sampled() const { return {state_->texture, state_}; }

    private:
        /// The one upload path both public entry points share (REMED-GFX-176). @p stride is the
        /// source row pitch in bytes; the destination region is always the whole of @p level.
        void UploadLevel(int level, const uint8_t* rgba, int levelW, int levelH, int stride);

        SdlGpuRenderer* owner_ = nullptr;
        // The actual GPU handle lives in this shared_ptr-owned struct, NOT directly here -- see
        // SdlGpuSampledTextureState's own doc comment for why.
        std::shared_ptr<SdlGpuSampledTextureState> state_;
        int width_ = 0;
        int height_ = 0;
        /// Mip levels SDL really allocated for this texture (REMED-GFX-176).
        int levelCount_ = 1;
        /// Identifies this texture in CNA_SDLGPU_TEXTURE_TRACE output; 0 when tracing is off.
        int traceId_ = 0;
    };

    /**
     * @brief `SDL_gpu`-backed `Texture3D` (Phase `SDLGPU-9`, `SDLGPU-40`/`SDLGPU-41`).
     *
     * A single `SDL_GPU_TEXTURETYPE_3D` texture, `SAMPLER` usage only (never a render target).
     * `mipMap` allocates the FNA-compatible authored mip chain; it does not request automatic mip
     * generation. `SetData`/`GetData` carry the explicit mip `level` and the 3D z/depth region
     * straight through to `SDL_GPUTextureRegion`, preserving exact texel data without a blit.
     */
    class SdlGpuTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        SdlGpuTexture3DRenderer(SdlGpuRenderer& owner, int width, int height, int depth, bool mipMap);
        ~SdlGpuTexture3DRenderer() override;

        SdlGpuTexture3DRenderer(const SdlGpuTexture3DRenderer&) = delete;
        SdlGpuTexture3DRenderer& operator=(const SdlGpuTexture3DRenderer&) = delete;

        /// REMED-GFX-135: true only once the upload command buffer has been submitted with the
        /// whole requested box copied into a transfer buffer this call owns; false for an
        /// out-of-range level or box, a null source, or a source buffer too small for the box. A
        /// genuine SDL API failure still throws -- that is a broken device, not an unsupported
        /// request. The transfer buffer is filled inside this call, so the caller's memory is never
        /// retained past it.
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: true only once the download fence has signalled and the whole requested
        /// box has been copied out of the transfer buffer; false for an empty request. Every other
        /// failure already throws, and the shared layer converts false into a deterministic
        /// System::NotSupportedException rather than fabricating a volume.
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

    private:
        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUTexture* texture_ = nullptr;
        int width_ = 0, height_ = 0, depth_ = 0;
        /// Mip levels SDL really allocated for this volume (REMED-GFX-135).
        int levelCount_ = 1;
    };

    // Real architectural fix for a render-target-destroyed-before-flush use-after-free: this
    // renderer batches all draws/clears and only actually renders them once, at Present() time
    // (EnsureFrameRendered). A RenderTarget2D destroyed as a short-lived local variable -- inside
    // one Draw() call, before Present() ever runs -- must still have ITS OWN pending Clear()/draws
    // execute correctly (e.g. if something else, like a SpriteBatch draw queued the same frame,
    // samples its contents). Since a pending pass segment / a queued DrawTarget may still need to
    // reference this target's GPU state well after the SdlGpuRenderTargetRenderer wrapper itself
    // has been destroyed, the actual GPU texture handles + first-use clear state live here, in a
    // separate, shared_ptr-owned struct -- NOT directly on the wrapper. Every pending
    // SdlGpuRenderer::PassSegment and every queued DrawTarget hold (or are kept alive by
    // something that holds) a shared_ptr to this struct, so it survives exactly as long as
    // anything still needs it, regardless of the wrapper's own C++ lifetime. This struct's own
    // destructor defers the actual GPU release via QueueTextureRelease (see that method's own doc
    // comment for why release itself must also be deferred, not just the C++ object holding the
    // handles).
    struct SdlGpuRenderTarget2DState
    {
        SdlGpuRenderer* owner = nullptr;
        int width = 0;
        int height = 0;
        bool mipMap = false;
        // The native `num_levels` allocated for colorTexture.  The deferred pass-finalization
        // path owns only this state (the public wrapper may already be gone), so it must use the
        // allocation fact rather than mipMap alone before asking SDL to generate a chain.
        int levelCount = 1;
        SDL_GPUTexture* colorTexture = nullptr;
        SDL_GPUTexture* msaaTexture = nullptr;
        SDL_GPUTexture* depthTexture = nullptr;
        // The real sample count msaaTexture (and depthTexture, when present) were created with --
        // 1 when msaaTexture is nullptr. Every pipeline drawing into this target's pass must be
        // created with a matching multisample_state.sample_count (SDLGPU-38's MSAA fix); stored
        // here rather than only on the SdlGpuRenderTargetRenderer wrapper because Render*Draws()
        // only ever sees this GPU-state struct via DrawTarget, not the (possibly already-destroyed)
        // wrapper -- see this struct's own doc comment above.
        SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1;
        // REMED-GFX-145: FIRST-USE clear state only, NOT "a Clear() is queued".
        //
        // These say "no render pass has ever written this resource, so a LOAD would read
        // uninitialized GPU memory" -- the very first segment that records this target therefore
        // begins with SDL_GPU_LOADOP_CLEAR of the values below, and consumes the flag. An explicit
        // GraphicsDevice.Clear() no longer lands here at all: it belongs to the ONE bind cycle it
        // was issued in and is carried by SdlGpuRenderer::PassSegment. Routing it through
        // the target instead was half of this finding's root cause -- the frame's LAST Clear() then
        // supplied the colour for the target's single merged pass.
        bool clearColorPending = true;
        bool clearDepthPending = true;
        bool clearStencilPending = false;
        SDL_FColor clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        float clearDepth = 1.0f;
        Uint8 clearStencil = 0;

        ~SdlGpuRenderTarget2DState();
    };

    /**
     * @brief `SDL_gpu`-backed `RenderTarget2D` (Phase `SDLGPU-8`, `SDLGPU-35`/`SDLGPU-38`).
     *
     * Owns a standalone `COLOR_TARGET | SAMPLER` texture rendered into its own render pass, one
     * per frame (see `SdlGpuRenderer::EnsureFrameRendered`'s per-target grouping) --
     * offscreen passes run before the swapchain pass so a target bound-then-unbound earlier in
     * the frame can safely be sampled by a later swapchain-targeted draw within the same frame.
     * MSAA (`SDLGPU-38`) resolves automatically via `SDL_GPUColorTargetInfo.resolve_texture` at
     * render-pass end -- no manual resolve step needed, same mechanism as
     * `SdlGpuRenderTargetCubeRenderer`'s own MSAA support. The actual GPU state lives in a separate,
     * shared_ptr-owned `SdlGpuRenderTarget2DState` (see that struct's own doc comment for why).
     */
    class SdlGpuRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        SdlGpuRenderTargetRenderer(SdlGpuRenderer& owner, int width, int height,
                                  int depthFormat, bool mipMap, int multiSampleCount);
        ~SdlGpuRenderTargetRenderer() override;

        SdlGpuRenderTargetRenderer(const SdlGpuRenderTargetRenderer&) = delete;
        SdlGpuRenderTargetRenderer& operator=(const SdlGpuRenderTargetRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return state_->width; }
        [[nodiscard]] int GetHeight() const override { return state_->height; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && state_->depthTexture != nullptr;
        }

        /** @brief Returns the sampleable (single-sample, resolved-into-if-MSAA) color texture. CNAEXT. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* ColorTexture() const { return state_->colorTexture; }
        /**
         * @brief Returns this target as a bindable, lifetime-safe sampled resource. CNAEXT.
         *
         * Always the single-sample colour texture, never `MsaaTexture()`: the multisample
         * attachment is created `COLOR_TARGET`-only and resolves INTO the colour texture at
         * render-pass end, so it is not a legal sampler binding at all (REMED-GFX-152).
         *
         * @return The resolved native handle paired with the shared state that keeps it alive.
         */
        CNAEXT [[nodiscard]] SdlGpuSampledTextureEXT Sampled() const { return {state_->colorTexture, state_}; }
        /** @brief Returns the multisampled render texture, or null when not multisampled. CNAEXT. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* MsaaTexture() const { return state_->msaaTexture; }
        /** @brief Returns the depth/stencil texture, or null when `DepthFormat::None` was requested. CNAEXT. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* DepthTexture() const { return state_->depthTexture; }
        /** @brief Whether a mip chain should be regenerated after this target's pass each frame. CNAEXT. */
        CNAEXT [[nodiscard]] bool WantsMipMap() const { return state_->mipMap; }
        /**
         * @brief Returns the shared, renderer-internal GPU state this target's actual rendering
         * operates on -- kept alive independent of this wrapper's own lifetime (see
         * `SdlGpuRenderTarget2DState`'s own doc comment for why). CNAEXT.
         */
        CNAEXT [[nodiscard]] const std::shared_ptr<SdlGpuRenderTarget2DState>& State() const { return state_; }
        /**
         * @brief Real GPU readback of this target's pixels (`SDLGPU-39`) -- reads from the
         * always-single-sample, sampleable color texture (already resolved-into if this target
         * is MSAA), a texture this renderer fully owns, unlike the swapchain -- so it does not hit
         * the swapchain-download segfault documented in `plan_sdlgpu.md`'s `SDLGPU-39` row.
         * Flushes any pending frame first so the read reflects this frame's draws.
         *
         * REMED-GFX-127: returns true only once the downloaded transfer buffer has been copied into
         * @p data in full. An empty region returns false rather than reporting success on a
         * destination it never wrote, and every real failure (transfer buffer, command buffer,
         * fence, map) still throws before any byte is copied.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data, int dataLength) const override;

        /**
         * @brief Adds this target as an extra colour attachment of the render-pass segment the
         * primary `rts[0]` bind just opened (SDLGPU-37: an "extra" MRT target — a real simultaneous
         * attachment of that ONE pass, but stock single-output draws still write attachment 0 only).
         *
         * REMED-GFX-145: the attachment set belongs to the bind CYCLE, not to the target, so it is
         * recorded on the segment rather than on either target's shared state — `{a,b}` followed by
         * `{a,c}` leaves `b` an attachment of the first segment instead of silently dropping it.
         * CNAEXT — internal use only.
         */
        CNAEXT void AttachToCurrentSegment();

    private:
        SdlGpuRenderer* owner_ = nullptr;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        // REMED-GFX-186: what SDL really allocated on colorTexture, so GetData can refuse a level
        // with no storage instead of handing SDL a mip index the resource does not own. Mirrors
        // SdlGpuRenderTargetCubeState's own levelCount, which is why the cube route answered this
        // question with a deterministic refusal while its 2D sibling segfaulted.
        int levelCount_ = 1;
        // The actual GPU texture handles + clear-pending state live in this shared_ptr-owned
        // struct, NOT directly here -- see SdlGpuRenderTarget2DState's own doc comment for why.
        std::shared_ptr<SdlGpuRenderTarget2DState> state_;
    };

    // Same rationale as SdlGpuRenderTarget2DState -- see that struct's own doc comment.
    struct SdlGpuRenderTargetCubeState
    {
        SdlGpuRenderer* owner = nullptr;
        int size = 0;
        bool mipMap = false;
        /// REMED-GFX-188: native `num_levels` on the resolved, single-sample cube texture.
        /// Pass finalization owns only this shared state, so allocation, mip generation and
        /// readback all consult the same fact even after the public wrapper has been destroyed.
        int levelCount = 1;
        SDL_GPUTexture* cubeTexture = nullptr;
        /**
         * REMED-GFX-141: SIX single-layer `SDL_GPU_TEXTURETYPE_2D` multisample textures, one per
         * cube face, where there used to be ONE shared by whichever face was currently active.
         *
         * They stay six separate textures rather than a six-layer array because SDL_gpu's own debug
         * validation asserts "For array textures: sample_count must be SDL_GPU_SAMPLECOUNT_1" -- a
         * real multisampled array texture is not a valid construction here at all (found
         * 2026-07-16 via SDLGPU-6's debug_mode wiring, which surfaced the previously-silent
         * violation as a genuine hang). Six independent textures give the same per-face isolation.
         *
         * The shared one could never be preserved: it had to be CYCLED on every pass (its previous
         * contents belonged to another face) and cycling is illegal together with
         * `SDL_GPU_LOADOP_LOAD`, so `RenderToTargetCubeFace` was forced to clear it every time.
         * With one texture per face there is nothing to cycle: the pass LOADs the face's own
         * samples and `SDL_GPU_STOREOP_RESOLVE_AND_STORE` keeps them for the next cycle.
         *
         * Cost is `size * size * samples * 4 * 6` bytes per cube target, allocated once at
         * construction -- no per-bind texture creation, no per-bind copy.
         */
        std::array<SDL_GPUTexture*, 6> msaaTextures{};
        /// Native `num_levels` shared by the six MSAA textures; zero when none were allocated.
        int msaaLevelCount = 0;
        SDL_GPUTexture* depthTexture = nullptr;
        /// Same rationale as SdlGpuRenderTarget2DState::sampleCount.
        SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1;
        /// REMED-GFX-141: this target's public RenderTargetUsage, collapsed by
        /// `RenderTargetUsagePreservesContentsEXT()`. It decides whether a multisampled face's pass
        /// keeps its own samples (`RESOLVE_AND_STORE`) or throws them away once resolved
        /// (`RESOLVE`) -- the single-sample path never needed it, because SDL_gpu's LOAD op on the
        /// cube texture itself already preserved a face by construction.
        bool preserveContents = false;
        /// REMED-GFX-145: per-face FIRST-USE clear state only — same meaning (and same reason) as
        /// SdlGpuRenderTarget2DState's own clear fields. An explicit Clear() belongs to the bind
        /// cycle that issued it and lives on SdlGpuRenderer::PassSegment instead.
        std::array<bool, 6> clearColorPending{true, true, true, true, true, true};
        std::array<bool, 6> clearDepthPending{true, true, true, true, true, true};
        std::array<bool, 6> clearStencilPending{};
        std::array<SDL_FColor, 6> clearColor{};
        std::array<float, 6> clearDepth{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        std::array<Uint8, 6> clearStencil{};

        ~SdlGpuRenderTargetCubeState();
    };

    /**
     * @brief `SDL_gpu`-backed `RenderTargetCube` (Phase `SDLGPU-8`, `SDLGPU-36`).
     *
     * Owns a single `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers). Only one face is ever the
     * active render target at a time (matches `D3D11RenderTargetCubeRenderer`'s/
     * `D3D12RenderTargetCubeRenderer`'s own shared-depth-buffer convention) -- clear/depth state is
     * tracked per face, but there is one shared depth texture reused across whichever face is
     * currently bound. MSAA resolves automatically via `SDL_GPUColorTargetInfo.resolve_texture`/
     * `resolve_layer` at render-pass end (no manual resolve step needed, unlike D3D11/D3D12).
     * The multisampled attachments are six independent, level-zero-only 2D textures because
     * `SDL_GPU_TEXTURETYPE_CUBE` has no multisampled variant and SDL_GPU forbids multisampled
     * arrays. REMED-GFX-188 regenerates only the resolved face's complete chain through clamped
     * per-level GPU blits immediately after that face's pass ends, so later passes can sample its
     * finalized mips. The actual GPU state lives in a separate, shared_ptr-owned
     * `SdlGpuRenderTargetCubeState` (see that struct's own doc comment, and
     * `SdlGpuRenderTarget2DState`'s, for why).
     */
    class SdlGpuRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        /// REMED-GFX-141: `preserveContents` is this target's public RenderTargetUsage. It reached
        /// `SdlGpuRenderer::CreateRenderTargetCube` under REMED-GFX-136 and was deliberately
        /// unused there, because a single-sample face was preserved by construction and a
        /// multisampled one could not be preserved at all. Now that each face owns its own
        /// multisample texture it selects that texture's store op.
        SdlGpuRenderTargetCubeRenderer(SdlGpuRenderer& owner, int size, int depthFormat,
                                      bool preserveContents, bool mipMap, int multiSampleCount);
        ~SdlGpuRenderTargetCubeRenderer() override;

        SdlGpuRenderTargetCubeRenderer(const SdlGpuRenderTargetCubeRenderer&) = delete;
        SdlGpuRenderTargetCubeRenderer& operator=(const SdlGpuRenderTargetCubeRenderer&) = delete;

        [[nodiscard]] int GetSize() const override { return state_->size; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Real GPU readback of one face's pixels (pulled forward from `SDLGPU-39` -- this
         * targets a texture this renderer fully controls, unlike the swapchain-download path that
         * segfaulted; see that row's notes in `plan_sdlgpu.md`). Flushes any pending frame first so
         * the read reflects this frame's draws, not stale/uninitialized GPU memory.
         */
        /// REMED-GFX-130: true only once the download fence has signalled and the whole requested
        /// face rectangle has been copied out of the transfer buffer; false for an empty request.
        /// REMED-GFX-134: also false for a mip level or rectangle this target never allocated --
        /// `SDL_DownloadFromGPUTexture` accepts an out-of-range `mip_level` without complaint and
        /// fills the transfer buffer with whatever it finds, so the guard has to be here.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the single-sample, sampleable cube texture. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* CubeTexture() const { return state_->cubeTexture; }
        /**
         * @brief Returns this cube target as a bindable, lifetime-safe sampled resource. CNAEXT.
         *
         * Always the single-sample cube texture that the per-face multisample attachments resolve
         * INTO, never one of those attachments — they are `COLOR_TARGET`-only and are not legal
         * sampler bindings, and a cube sampler needs the cube-typed resource in any case
         * (REMED-GFX-152).
         *
         * @return The resolved native handle paired with the shared state that keeps it alive.
         */
        CNAEXT [[nodiscard]] SdlGpuSampledTextureEXT Sampled() const { return {state_->cubeTexture, state_}; }
        /**
         * @brief Returns one cube face's own multisample render texture. CNAEXT.
         *
         * REMED-GFX-141: there are six, one per face, where this used to return the single texture
         * every face shared.
         *
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @return That face's multisample texture, or null when this target is not multisampled or
         *         @p face is out of range.
         */
        CNAEXT [[nodiscard]] SDL_GPUTexture* MsaaTexture(int face) const
        {
            return face >= 0 && face < 6 ? state_->msaaTextures[static_cast<std::size_t>(face)]
                                         : nullptr;
        }
        /** @brief Native level count of one face's MSAA attachment, or zero when absent. CNAEXT. */
        CNAEXT [[nodiscard]] int MsaaLevelCountEXT(int face) const
        {
            return MsaaTexture(face) != nullptr ? state_->msaaLevelCount : 0;
        }
        /** @brief Returns the shared depth/stencil texture, or null when `DepthFormat::None` was requested. CNAEXT. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* DepthTexture() const { return state_->depthTexture; }
        /** @brief Whether the mip chain should be regenerated after this cube's faces render each frame. CNAEXT. */
        CNAEXT [[nodiscard]] bool WantsMipMap() const { return state_->mipMap; }
        /**
         * @brief Returns the number of mip levels SDL really allocated for the resolved cube. CNAEXT.
         *
         * @return The native `num_levels` used to create the single-sample cube texture.
         */
        CNAEXT [[nodiscard]] int LevelCountEXT() const { return state_->levelCount; }
        /** @brief Returns the shared, renderer-internal GPU state this cube's rendering operates
         * on -- kept alive independent of this wrapper's own lifetime. CNAEXT. */
        CNAEXT [[nodiscard]] const std::shared_ptr<SdlGpuRenderTargetCubeState>& State() const { return state_; }

    private:
        SdlGpuRenderer* owner_ = nullptr;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        std::shared_ptr<SdlGpuRenderTargetCubeState> state_;
    };

    /**
     * @brief `SDL_gpu`-backed plain (non-render-target), uploaded `TextureCube` (Phase `SDLGPU-9`,
     * `SDLGPU-51`).
     *
     * A single `SDL_GPU_TEXTURETYPE_CUBE` texture, `SAMPLER` usage only (never a render target).
     * Each face is one of the cube texture's 6 array layers -- `SetData`/`GetData` both carry the
     * face straight through to `SDL_GPUTextureRegion.layer` (matching
     * `SdlGpuRenderTargetCubeRenderer::GetData`'s own convention), and the mip `level` straight
     * through to `mip_level`, same as `SdlGpuTexture3DRenderer`. Uploads use `cycle=false` for the
     * same reason `SdlGpuTexture3DRenderer::SetData` does: a real cube map is built up via multiple
     * independent per-face (and potentially per-level) `SetData` calls that must all land on the
     * SAME underlying resource -- `cycle=true` would silently orphan earlier faces' writes (the
     * bug `SDLGPU-40` found and fixed for `Texture3D`).
     */
    class SdlGpuTextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        SdlGpuTextureCubeRenderer(SdlGpuRenderer& owner, int size, bool mipMap);
        ~SdlGpuTextureCubeRenderer() override;

        SdlGpuTextureCubeRenderer(const SdlGpuTextureCubeRenderer&) = delete;
        SdlGpuTextureCubeRenderer& operator=(const SdlGpuTextureCubeRenderer&) = delete;

        /// REMED-GFX-135: same explicit completion contract as SdlGpuTexture3DRenderer::SetData.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: true only once the download fence has signalled and the whole requested
        /// face rectangle has been copied out of the transfer buffer; false for an empty request.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying `SDL_GPUTexture`. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* Texture() const { return state_->texture; }
        /**
         * @brief Returns this cube texture as a bindable, lifetime-safe sampled resource. CNAEXT.
         *
         * @return The native handle paired with the shared state that keeps it alive.
         */
        CNAEXT [[nodiscard]] SdlGpuSampledTextureEXT Sampled() const { return {state_->texture, state_}; }

    private:
        SdlGpuRenderer* owner_ = nullptr;
        // Same rationale as SdlGpuTextureRenderer's own state_ -- see SdlGpuSampledTextureState.
        std::shared_ptr<SdlGpuSampledTextureState> state_;
        int size_ = 0;
        bool mipMap_ = false;
        /// Mip levels SDL really allocated for this cube (REMED-GFX-135).
        int levelCount_ = 1;
    };

    /**
     * @brief Resolves any public 2D texture resource to its bindable native form. CNAEXT.
     *
     * REMED-GFX-152: the ONE place this renderer is allowed to decide what a `const ITextureRenderer*`
     * really is. Every SpriteBatch and stock/custom 3D binding route goes through here, so a
     * `RenderTarget2D` can never again reach a route that assumed `SdlGpuTextureRenderer`. The
     * decision is made by `dynamic_cast` — enforced by the object itself — not by a static
     * assumption, and an unrecognised renderer throws deterministically instead of being
     * reinterpreted.
     *
     * @param texture Renderer texture to resolve; null yields an empty (falsy) result, which is how
     *                an optional slot such as a PBR normal map says "not supplied".
     * @param usage   Public API name used in the exception text, e.g. `"BasicEffect.Texture"`.
     * @return The sampleable native handle plus a reference keeping its owner alive.
     * @throws std::invalid_argument If @p texture belongs to a different graphics renderer, or to a
     *         resource this renderer cannot expose as a sampleable 2D texture.
     */
    CNAEXT [[nodiscard]] SdlGpuSampledTextureEXT ResolveSampledTextureEXT(const ITextureRenderer* texture,
                                                                         const char* usage);

    /**
     * @brief Resolves any public cube texture resource to its bindable native form. CNAEXT.
     *
     * The `ITextureCubeRenderer` counterpart of @ref ResolveSampledTextureEXT, covering both an
     * uploaded `TextureCube` and a rendered-into `RenderTargetCube`.
     *
     * @param texture Renderer cube texture to resolve; null yields an empty (falsy) result.
     * @param usage   Public API name used in the exception text, e.g. `"EnvironmentMapEffect.EnvironmentMap"`.
     * @return The sampleable native cube handle plus a reference keeping its owner alive.
     * @throws std::invalid_argument If @p texture belongs to a different graphics renderer, or to a
     *         resource this renderer cannot expose as a sampleable cube texture.
     */
    CNAEXT [[nodiscard]] SdlGpuSampledTextureEXT ResolveSampledCubeEXT(const ITextureCubeRenderer* texture,
                                                                      const char* usage);

    /** @brief `SDL_gpu`-backed vertex buffer. */
    class SdlGpuVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        SdlGpuVertexBufferRenderer(SdlGpuRenderer& owner, int vertexCapacity);
        ~SdlGpuVertexBufferRenderer() override;

        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        /**
         * @brief REMED-GFX-DECL-GUARD: remembers the declaration this buffer carries.
         *
         * This renderer still selects its `SDL_GPUVertexAttribute` set from the byte stride
         * (REMED-GFX-217). Storing the declaration is what lets a draw refuse one that layout
         * would silently reinterpret, without translating it.
         *
         * @param vertexDeclaration The declaration the caller propagated for this buffer.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        /**
         * @brief SDLGPU-23: real `Discard`/`NoOverwrite` streaming hints, not the
         * `IGraphicsRenderer` default (which ignores @p options and calls the plain `SetData()`).
         * Maps to `SDL_UploadToGPUBuffer`'s own `cycle` flag exactly like `EasyGLVertexBufferRenderer`'s
         * established `Discard`→orphan/`NoOverwrite`→in-place convention: `Discard`/`None` cycle the
         * buffer to a fresh backing resource (avoids a GPU stall on any in-flight read of the old
         * data, `SDL_gpu.h`'s own documented `cycle=true` behavior); `NoOverwrite` does not cycle
         * (the caller's own promise that no in-flight GPU read is being overwritten).
         */
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Returns the underlying `SDL_GPUBuffer`. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUBuffer* Buffer() const { return buffer_; }
        /** @brief Returns the vertex stride in bytes from the most recent `SetData()`. CNAEXT. */
        CNAEXT [[nodiscard]] std::size_t Stride() const { return stride_; }
        // CPU-side copy of the most recent SetData() upload -- needed because DrawColoredPrimitives()/
        // DrawPrimitivesEx() render lazily at Present() time, but a caller like
        // GraphicsDevice::DrawUserPrimitives() typically uses a function-local temporary
        // IVertexBufferRenderer already destroyed by then (matches WebGPUVertexBufferRenderer's own
        // ShadowData() rationale).
        CNAEXT [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }
        /** @brief The declaration this buffer carries, for REMED-GFX-DECL-GUARD. CNAEXT. */
        CNAEXT [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& Declaration() const
        {
            return declaration_;
        }

    private:
        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        Uint32 capacityBytes_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> shadowData_;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /** @brief `SDL_gpu`-backed 16- or 32-bit index buffer. */
    class SdlGpuIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        SdlGpuIndexBufferRenderer(SdlGpuRenderer& owner, int indexCapacity, bool thirtyTwoBit);
        ~SdlGpuIndexBufferRenderer() override;

        void SetData16(const void* data, int indexCount) override;
        void SetData32(const void* data, int indexCount) override;
        /** @brief SDLGPU-23: real streaming hint, same rationale as `SdlGpuVertexBufferRenderer::SetDataWithOptions`. */
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        /** @brief SDLGPU-23: real streaming hint, same rationale as `SdlGpuVertexBufferRenderer::SetDataWithOptions`. */
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Returns the underlying `SDL_GPUBuffer`. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUBuffer* Buffer() const { return buffer_; }
        /** @brief CPU-side copy of the most recent upload. CNAEXT — see SdlGpuVertexBufferRenderer::ShadowData(). */
        CNAEXT [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        void Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit, bool cycle);

        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        Uint32 capacityBytes_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint8_t> shadowData_;
    };

    /**
     * @brief `SDL_gpu`-backed custom `ShaderEffect` (Phase `SDLGPU-10`, `SDLGPU-42`/`SDLGPU-43`).
     *
     * Compiles arbitrary user-authored GLSL vertex+fragment source to SPIR-V at RUNTIME via
     * `libshaderc` (`SDL_gpu` only accepts precompiled bytecode, unlike EasyGL's native GLSL
     * driver compile) -- unlike `SDLGPU-13`'s build-time-only `compile_shaders.py` decision for
     * CNA's own built-in effect shaders, an arbitrary user `ShaderEffect`'s source isn't known
     * until the game runs, so `libshaderc` is linked into the real renderer binary here, not just
     * the build-time script.
     *
     * Fixed vertex contract, matching every other renderer's own custom-`ShaderEffect` convention
     * (`VulkanEffectRenderer`/`D3D11EffectRenderer`/`D3D12EffectRenderer`): `SpriteVertex`-shaped
     * (pos `vec2` @0, uv `vec2` @8, color `vec4` @16, 32 bytes) -- a `SpriteBatch`-custom-shader
     * facility, not a general arbitrary-vertex-format one (see
     * `ISpriteBatchRenderer::SetCustomEffect`'s own doc comment).
     *
     * Fixed 128-byte uniform layout, mirroring `D3D11EffectRenderer`'s own byte-for-byte convention
     * (both vertex and fragment stages get their own copy, matching this renderer's established
     * "same PC struct in both stages" convention): `[0..15]` = vpSize (`vec4`, xy used, zw pad for
     * std140 alignment; set automatically once per sprite render via `SetViewportSizeEXT` -- the
     * game/effect author never calls it directly), `[16..79]` = `mat4` matrix, `[80..95]` = `vec4`
     * color, `[96..99]` = float/int slot 0 (`name` is deliberately ignored, matching every sibling
     * `EffectRenderer`'s own name-discarding convention). Per `SDL_gpu`'s own binding
     * convention (`SDL_CreateGPUShader`'s doc comment), the compiled GLSL must declare its uniform
     * block at vertex `set=1`/`binding=0` and fragment `set=3`/`binding=0`, and its one texture
     * sampler at fragment `set=2`/`binding=0`.
     */
    class SdlGpuEffectRenderer final : public IEffectRenderer
    {
    public:
        explicit SdlGpuEffectRenderer(SdlGpuRenderer& owner);
        ~SdlGpuEffectRenderer() override;

        SdlGpuEffectRenderer(const SdlGpuEffectRenderer&) = delete;
        SdlGpuEffectRenderer& operator=(const SdlGpuEffectRenderer&) = delete;

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        /** @brief No-op -- this renderer defers the actual pipeline bind to `RenderSprites()` at
         * `Present()` time, using a per-`SpriteCommand` snapshot of this object's uniform state
         * (see `SdlGpuRenderer::QueueSprite`'s own custom-effect handling). */
        void Bind() override {}
        /** @brief No-op, same rationale as `Bind()`. */
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return valid_; }
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;

        /** @brief Writes the `[0..15]`-byte vpSize slot -- called once per sprite render by
         * `SdlGpuRenderer::QueueSprite`, mirroring every sibling `EffectRenderer`'s own
         * "set automatically by the sprite-batch runtime" convention. CNAEXT. */
        CNAEXT void SetViewportSizeEXT(float width, float height);
        /** @brief Returns the pipeline for @p colorFormat / @p sampleCount /
         * @p depthStencilFormat / @p colorTargetCount,
         * compiling+caching it on first use. @p colorTargetCount > 1 (real MRT, SDLGPU-37) builds
         * a pipeline with that many `color_target_descriptions`, all sharing @p colorFormat (every
         * `RenderTarget2D` in this renderer is `R8G8B8A8_UNORM`) -- lets a custom multi-output
         * fragment shader (the only kind of shader in this codebase that can genuinely write more
         * than one attachment) really render simultaneous MRT. @p depthStencilFormat is
         * `SDL_GPU_TEXTUREFORMAT_INVALID` when the active pass is genuinely depthless; it is part
         * of pipeline compatibility and cache identity (REMED-GFX-097). @p depthBias and
         * @p slopeScaleDepthBias are the queued sprite's by-value rasterizer snapshot and are
         * pipeline-static identity/state (REMED-GFX-051). Null if `CompileProgram()` did not
         * succeed. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipeline(SDL_GPUTextureFormat colorFormat,
                                                                          SDL_GPUSampleCount sampleCount,
                                                                          SDL_GPUTextureFormat depthStencilFormat,
                                                                          int colorTargetCount,
                                                                          const std::array<int, 4>& colorWriteMasks,
                                                                          float depthBias,
                                                                          float slopeScaleDepthBias);
        /** @brief Number of cached custom-effect pipelines. Test-only cache-identity
         * introspection for REMED-GFX-051. CNAEXT. */
        CNAEXT [[nodiscard]] std::size_t GetPipelineCacheSizeEXT() const
        {
            return pipelines_.size();
        }
        /** @brief Returns a snapshot of the current 128-byte uniform block. CNAEXT — internal use
         * only (called once per queued sprite, so later `SetUniform*` calls on the live effect
         * object don't retroactively change an already-queued sprite's rendered result). */
        CNAEXT [[nodiscard]] std::array<float, 32> SnapshotUniforms() const { return pushConst_; }

    private:
        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUShader* vertexShader_ = nullptr;
        SDL_GPUShader* fragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> pipelines_;
        std::array<float, 32> pushConst_{};
        std::string compileError_;
        bool valid_ = false;
    };

    /** @brief `SDL_gpu`-backed `SpriteBatch`. Queues quads; actual draws happen at Present() time. */
    class SdlGpuSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit SdlGpuSpriteBatchRenderer(SdlGpuRenderer& owner);
        ~SdlGpuSpriteBatchRenderer() override = default;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override { addressU_ = addressU; addressV_ = addressV; }
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        SdlGpuRenderer* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        /// Set at Begin() time (SetCustomEffect), cleared at End() -- SDLGPU-42/43. Resolved to the
        /// concrete SdlGpuEffectRenderer* and snapshotted per-sprite at Draw() time (see QueueSprite),
        /// not read again at Present() time, so later SetUniform* calls on the same live effect
        /// object never retroactively change an already-queued sprite's rendered result.
        Effect* customEffect_ = nullptr;
    };

    /**
     * @brief `SDL_gpu`-backed graphics renderer (`CNA_GRAPHICS_RENDERER=SDL_GPU`).
     *
     * See `plan_sdlgpu.md` for the phased implementation plan. As of Phase `SDLGPU-6`, device/
     * window/swapchain lifecycle, color+depth+stencil clear/present, `Texture2D`, vertex/index
     * buffers, `SpriteBatch`, and the core 3D vertex formats (`colored3d`/`textured3d`/
     * `colored_textured3d`/`lit_textured3d`, i.e. `BasicEffect`) are real and verified.
     * `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific
     * `GpuDrawParams` fields (`alphaTest`, `dualTexture`, `envMapping`, `skinned`) are not yet
     * checked by this renderer's `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch — such a
     * draw currently renders as a plain `BasicEffect` draw instead (later phases add real
     * per-effect dispatch, matching `WebGPURenderer`'s own precedent).
     */
    class SdlGpuRenderer final : public IGraphicsRenderer
    {
        friend class SdlGpuRenderTargetRenderer;
        friend class SdlGpuRenderTargetCubeRenderer;
        friend struct SdlGpuRenderTarget2DState;
        friend struct SdlGpuRenderTargetCubeState;
        // REMED-GFX-152: ordinary uploaded textures defer their native release through the same
        // QueueTextureRelease path render targets already used.
        friend struct SdlGpuSampledTextureState;
        friend class SdlGpuEffectRenderer;
    public:
        /** @brief Vertex layout for the `sprite2d` pipeline: position, UV, RGBA color (32 bytes). */
        struct SpriteVertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        /**
         * @brief The "no segment has ever been opened" id. REMED-GFX-145/143.
         *
         * Real segment ids start at 1. This used to be the id every draw issued with no render
         * target bound carried, because the swapchain had no bind cycles of its own and its single
         * trailing pass asked for this value to mean "no segment filter at all". REMED-GFX-143
         * gave the backbuffer real segments, so a backbuffer draw now carries the id of the cycle
         * it was issued in and this value survives only as the pre-first-cycle sentinel.
         */
        static constexpr std::uint64_t kSwapchainSegment = 0;

        // Identifies which render pass a queued draw/sprite belongs to: the swapchain (both null,
        // face -1), a RenderTarget2D (rt set), or one face of a RenderTargetCube (cube + face
        // set). At most one of rt/cube is ever set, matching this renderer's single-current-target
        // semantics (SetRenderTarget2D/SetRenderTargetCubeFace are mutually exclusive).
        // Points at the shared GPU-state struct, NOT the SdlGpuRenderTargetRenderer/
        // SdlGpuRenderTargetCubeRenderer wrapper -- a DrawCommand carrying this must remain valid
        // to compare/render even if the wrapper is destroyed before Present() (see
        // SdlGpuRenderTarget2DState's own doc comment). Safe as a raw (non-owning) pointer here
        // because passSegments_ holds the actual owning shared_ptr for the entire duration of one
        // EnsureFrameRendered() call.
        struct DrawTarget
        {
            const SdlGpuRenderTarget2DState* rt = nullptr;
            const SdlGpuRenderTargetCubeState* cube = nullptr;
            int face = -1;

            bool operator==(const DrawTarget& other) const
            {
                return rt == other.rt && cube == other.cube && face == other.face;
            }
            bool operator!=(const DrawTarget& other) const { return !(*this == other); }
        };

        /**
         * @brief REMED-GFX-145: ONE public render-target bind/unbind cycle, i.e. exactly one
         * native `SDL_BeginGPURenderPass`/`SDL_EndGPURenderPass` pair.
         *
         * The deferred queues used to carry only a `DrawTarget`, and `EnsureFrameRendered` gave
         * each DISTINCT target one pass holding every draw ever queued against it that frame. Two
         * public cycles of one target were then indistinguishable, so the second cycle's load
         * action never happened, its `Clear()` ran before the first cycle's draws, and `A -> B -> A`
         * became two passes instead of three. A segment is opened by every bind (including a rebind
         * of the same resource, face, mip or MRT set), closed by every unbind, and appended to
         * `passSegments_` in exactly the order the cycles were opened -- so replay is public order
         * by construction, with no sort and no per-draw allocation.
         */
        struct PassSegment
        {
            std::uint64_t id = 0;
            /**
             * At most one of `rt` / `cube` is set; `cube` also carries `face`. REMED-GFX-143: with
             * NEITHER set the segment is a BACKBUFFER bind cycle. The backbuffer used to have no
             * segment at all -- `currentSegment_` simply held `kSwapchainSegment` while no target
             * was bound, and `EnsureFrameRendered` recorded every backbuffer draw of the frame in
             * one trailing swapchain pass no matter when it was issued.
             */
            std::shared_ptr<SdlGpuRenderTarget2DState> rt;
            std::shared_ptr<SdlGpuRenderTargetCubeState> cube;
            int face = -1;
            /// SDLGPU-37 real MRT: `rts[1..]` of the `SetRenderTargets` call that opened THIS cycle.
            std::vector<std::shared_ptr<SdlGpuRenderTarget2DState>> extraAttachments;
            /// An explicit GraphicsDevice.Clear() issued while this cycle was bound. Within one
            /// segment the last Clear() still wins, because SDL_gpu delivers the colour through the
            /// pass load op and a pass has exactly one. REMED-GFX-156: which is why a Clear() that
            /// FOLLOWS a draw of this segment no longer lands here at all -- it opens a new segment
            /// (see SegmentForOrderedClear), so several Clear()s of one bind cycle only ever share
            /// a segment while nothing observable separates them.
            bool clearColorRequested = false;
            bool clearDepthRequested = false;
            bool clearStencilRequested = false;
            SDL_FColor clearColor{0.0f, 0.0f, 0.0f, 1.0f};
            float clearDepth = 1.0f;
            Uint8 clearStencil = 0;
            /**
             * @brief REMED-GFX-156: has any draw been queued into this segment yet?
             *
             * Set by PushDrawOrder, the one choke point every queued draw and sprite routes
             * through, and read only by SegmentForOrderedClear to decide whether the next Clear()
             * is observable (a draw precedes it, so it needs its own pass) or merely another
             * leading clear (it can fold into this segment's load action).
             */
            bool hasDraws = false;
            /**
             * @brief REMED-GFX-156: this segment was opened by an ordered Clear(), not by a bind.
             *
             * Purely descriptive -- it exists so the native order trace can distinguish a public
             * bind cycle from the pass segments one cycle was split into, and so the pass-count
             * assertions in the tests can state which passes the split is responsible for.
             */
            bool openedByClear = false;
        };

        // Adversarial-review finding #4: which per-family queue a QueuedDrawRef points into.
        enum class DrawKind : Uint8
        {
            Colored, Textured, LitTextured, AlphaTest, DualTexture, EnvMap, Skinned, Sprite, Pbr
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
            // plan_fx.md FX-071: guarded like every other compiled-effect member in this header,
            // so a build without the option never declares an enumerator no switch handles.
            , CompiledEffect
#endif
        };

        // A single entry in drawOrder_ (see that field's own doc comment) -- identifies one queued
        // draw command by which family vector it lives in and its index there.
        struct QueuedDrawRef
        {
            DrawKind kind;
            std::size_t index;
            // REMED-GFX-145: the logical render-pass segment (one public bind/unbind cycle) this
            // draw was issued inside; 0 = the swapchain. Target IDENTITY is not enough to group
            // draws into passes -- two bind cycles of ONE target are two passes with two load
            // actions, two clear states and two viewport/scissor regimes, and grouping by
            // DrawTarget alone silently merged them. Assigned in PushDrawOrder from
            // currentSegment_, the single choke point every Queue*Draw()/QueueSprite() already
            // routes through.
            std::uint64_t segment = 0;
            // REMED-GFX-064: the GraphicsDevice.Viewport live when this draw was queued, captured
            // per-draw (PushDrawOrder) and replayed via SDL_SetGPUViewport in RenderQueuedDraws.
            // viewportSet=false (or a zero-size rect) => the pass's full render-target extents.
            bool viewportSet = false;
            int viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0;
            float viewportMinDepth = 0.0f, viewportMaxDepth = 1.0f;
            // REMED-GFX-068: the RasterizerState.ScissorTestEnable + GraphicsDevice.ScissorRectangle
            // live when this draw was queued, captured per-draw (PushDrawOrder) and replayed via
            // SDL_SetGPUScissor in RenderQueuedDraws (ApplyScissorForRef). Per-draw for the same
            // deferred-model reason as the viewport: SetRenderTarget resets ScissorRectangle to the
            // full target on bind/unbind, so a per-pass read of the live scissor at Present would be
            // the post-unbind full-backbuffer rect, not the sub-rect an RT draw was issued under.
            // scissorEnabled=false (or a zero-size rect) => the pass's full render-target extents
            // (no clip), matching RasterizerState.ScissorTestEnable=false.
            bool scissorEnabled = false;
            int scissorX = 0, scissorY = 0, scissorW = 0, scissorH = 0;
            // REMED-GFX-069: the GraphicsDevice.BlendFactor (constant blend color) live when this
            // draw was queued, captured per-draw (PushDrawOrder) and replayed via
            // SDL_SetGPUBlendConstants in RenderQueuedDraws (ApplyBlendFactorForRef). Per-draw for
            // the same deferred-model reason as viewport/scissor: this renderer replays draws at
            // Present, so a single live-member read there would give every queued draw the LAST
            // BlendFactor set that frame, not the one each draw was issued under. Normalized 0..1
            // linear factors (already divided by 255 by GraphicsDevice::setBlendFactorProperty);
            // default 1,1,1,1 = XNA's GraphicsDevice.BlendFactor default (Color::White), so draws
            // that predate any SetBlendFactor use White (correct XNA default) rather than SDL_gpu's
            // arbitrary, driver-dependent uninitialized blend constant.
            float blendFactorR = 1.0f, blendFactorG = 1.0f, blendFactorB = 1.0f, blendFactorA = 1.0f;
        };

        // SDLGPU-18: raw XNA Blend/BlendFunction ordinals, captured at Queue*Draw time so the
        // exact requested blend equation (not just enabled/disabled) is baked into the pipeline
        // that command actually renders with. Defaults mirror BlendState.Opaque (One/Zero/Add).
        struct BlendKeyParams
        {
            int colorSrc = 0, colorDst = 1, alphaSrc = 0, alphaDst = 1, colorFunc = 0, alphaFunc = 0;
        };

        // SDLGPU-19: raw XNA StencilOperation/CompareFunction ordinals for real front/back
        // stencil-op pipeline baking. Defaults mirror DepthStencilState.Default (stencil disabled).
        struct StencilKeyParams
        {
            bool enable = false;
            int func = 0, fail = 0, depthFail = 0, pass = 0;
            bool twoSided = false;
            int ccwFunc = 0, ccwFail = 0, ccwDepthFail = 0, ccwPass = 0;
            // DepthStencilState.StencilMask/StencilWriteMask -- real XNA default is 0x7FFFFFFF
            // (DepthStencilState's own field default), truncated to the low byte SDL_gpu's
            // compare_mask/write_mask (Uint8) actually hold, i.e. 0xFF.
            int readMask = 0xFF, writeMask = 0xFF;
        };

        // SDLGPU-18/19/20 + REMED-GFX-051: snapshot of BlendState/the stencil half of
        // DepthStencilState/RasterizerState's pipeline-static fields, captured at Queue*Draw time
        // -- mirrors DrawTarget's
        // own "one shared struct field per DrawCommand" precedent. depthTest/depthWrite/depthFunc
        // stay as each DrawCommand's own pre-existing, separate fields (unchanged) since those
        // predate this and are already correctly threaded through; this struct only carries the
        // dimensions added by those remediations. ScissorTestEnable is deliberately NOT here:
        // its rect+enable are per-draw pass state carried in QueuedDrawRef instead
        // (REMED-GFX-068, applied via SDL_SetGPUScissor in ApplyScissorForRef, exactly like the
        // viewport), not baked into the pipeline. SDL_GPU depth bias has no dynamic setter, so
        // both public bias floats must live here and be replayed into the pipeline selected for
        // this exact queued draw (REMED-GFX-051).
        struct RenderStateSnapshot
        {
            bool blendEnabled = false;
            BlendKeyParams blend;
            // REMED-GFX-077/-098: slot-aligned XNA ColorWriteChannels (bit0=R..bit3=A), baked
            // into the corresponding SDL_GPU target description and cache identity. 15 = All.
            std::array<int, 4> colorWriteMasks{{15, 15, 15, 15}};
            int cullMode = 0;   // XNA CullMode ordinal; 0 = None
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            StencilKeyParams stencil;
            // SDL_gpu exposes this as a genuine per-draw dynamic value (SDL_SetGPUStencilReference),
            // not a pipeline-baked one -- captured per-command like the rest of this snapshot. (The
            // scissor is likewise per-draw dynamic via SDL_SetGPUScissor, but it lives in
            // QueuedDrawRef with the viewport rather than here -- REMED-GFX-068.)
            int stencilReference = 0;
        };

        // SDLGPU-21: one XNA GraphicsDevice.SamplerStates[slot] snapshot, stored per-slot by
        // ApplySamplerState() and read directly (not via RenderStateSnapshot) into each
        // DrawCommand's own pre-existing textureFilter/addressU/addressV fields at Queue*Draw
        // time. Defaults mirror SamplerState.LinearWrap, the real XNA GraphicsDevice default
        // (SpriteCommand's own defaults deliberately differ -- SpriteBatch's real XNA default is
        // SamplerState.LinearClamp, a distinct, correct behavioral difference, not an inconsistency).
        struct SamplerSlotState
        {
            int filter = 0;
            int addressU = 0;
            int addressV = 0;
            int maxAnisotropy = 4;
        };

        struct SpriteCommand
        {
            // REMED-GFX-152: the same resolved, lifetime-owning value every 3D binding route now
            // carries, so SpriteBatch and the effects share one texture-resolution contract. It
            // replaces a bare SDL_GPUTexture*, which was correct about the TYPE question (a sprite
            // source may be a plain Texture2D or a rendered-into target, unrelated classes both) but
            // silent about the LIFETIME one -- this command is replayed at Present, by when a
            // short-lived public texture may already have released that handle.
            SdlGpuSampledTextureEXT texture;
            std::array<SpriteVertex, 6> vertices{};
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            DrawTarget target;  ///< default = swapchain
            // SDLGPU-42/43: non-null if this sprite was queued during a SetCustomEffect(effect)
            // Begin/End cycle with a validly-compiled custom shader -- customUniforms is a snapshot
            // of the effect's uniform state AT QUEUE TIME (see QueueSprite), not read again at
            // Present() time, so later SetUniform* calls on the same live effect object never
            // retroactively change this already-queued sprite's rendered result.
            SdlGpuEffectRenderer* customEffect = nullptr;
            std::array<float, 32> customUniforms{};
            // SDLGPU-18/19/20: SpriteBatch.Begin() sets GraphicsDevice.BlendState/DepthStencilState/
            // RasterizerState the same way any other draw does (defaulting to
            // BlendState.AlphaBlend/DepthStencilState.None/RasterizerState.CullCounterClockwise
            // per SpriteBatch.Begin()'s own real XNA defaults), so sprites bake the same
            // dynamically-captured state as every 3D shader family, not a hardcoded blend/no-depth.
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;
        };

        /**
         * @brief REMED-GFX-117: the native arguments one indexed draw submits.
         *
         * Resolved once from the public XNA draw parameters and then carried by value on the draw
         * command, so a deferred draw keeps its own range. All three are in SDL_GPU's own units:
         * `firstIndex` is an index *element* offset (never a byte offset), `vertexOffset` is the
         * signed value SDL adds to each decoded index before vertex fetch, and `indexCount` is the
         * exact topology-derived number of indices the draw consumes.
         */
        struct NativeIndexedRange
        {
            Uint32 indexCount = 0;    ///< SDL_DrawGPUIndexedPrimitives `num_indices`
            Uint32 firstIndex = 0;    ///< SDL_DrawGPUIndexedPrimitives `first_index`
            Sint32 vertexOffset = 0;  ///< SDL_DrawGPUIndexedPrimitives `vertex_offset`
        };

        // Phase SDLGPU-6: colored3d/textured3d/colored_textured3d/lit_textured3d draw commands.
        // Vertex/index data is shadow-copied at Draw-call time (see
        // SdlGpuVertexBufferRenderer::ShadowData()'s own rationale) and re-uploaded into a
        // transient SDL_GPUBuffer during the copy pass that precedes each frame's render pass
        // (see UploadSceneDrawData/ReleaseSceneDrawBuffers). No fog (deliberately deferred, same
        // as this codebase's WebGPU renderer's own initial 3D vertical slice).
        struct ColoredDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;  ///< empty for a non-indexed draw
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};  ///< mirrors VulkanRenderer::FillExtPushConst()'s 128-byte layout
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;  ///< transient, set by UploadSceneDrawData
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;   ///< transient, set by UploadSceneDrawData
        };

        // textured3d (stride 20, VertexPositionTexture) and colored_textured3d (stride 24,
        // VertexPositionColorTexture) share this command shape and the same fragment shader --
        // `hasVertexColor` selects the stride-24 vertex-input-state/pipeline vs stride-20's.
        struct TexturedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture;  ///< REMED-GFX-152: resolved once, at queue time
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            /// REMED-GFX-170: captured with the filter, so a queued draw cannot observe a
            /// later ApplySamplerState. XNA SamplerState.MaxAnisotropy default.
            int maxAnisotropy = 4;
            bool hasVertexColor = false;  ///< stride 24 vs stride 20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // lit_textured3d (stride 32, VertexPositionNormalTexture) -- real Blinn-Phong lighting.
        struct LitTexturedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};
            std::array<float, 56> lightUniforms{};  ///< LitLightParams: 10 vec4 + 1 mat4 = 224 bytes
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture;  ///< REMED-GFX-152: resolved once, at queue time
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            /// REMED-GFX-170: captured with the filter, so a queued draw cannot observe a
            /// later ApplySamplerState. XNA SamplerState.MaxAnisotropy default.
            int maxAnisotropy = 4;
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // AlphaTestEffect (Phase SDLGPU-7) -- strides 20 (VertexPositionTexture)/32
        // (VertexPositionNormalTexture, normal unread) share one shader (alphaTestVertexShader_);
        // stride 24 (VertexPositionColorTexture, vertex-color tint) uses
        // alphaTestColoredVertexShader_. `stride` selects which at render time.
        struct AlphaTestDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};  ///< [20..23]=alphaTest params, [24]=vertexColorEnabled (no lighting/ambient slots needed)
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture;  ///< REMED-GFX-152: resolved once, at queue time
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            /// REMED-GFX-170: captured with the filter, so a queued draw cannot observe a
            /// later ApplySamplerState. XNA SamplerState.MaxAnisotropy default.
            int maxAnisotropy = 4;
            std::size_t stride = 20;  ///< 20, 24, or 32 -- selects vertex layout + shader
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // DualTextureEffect (Phase SDLGPU-7) -- two texture units sampled at the same UV
        // (`tex1.rgb*=2; result=tex1*tex2*tint`). Strides 20/24 use dedicated vertex shaders
        // (dualTextureVertexShader_/dualTextureColoredVertexShader_); the fragment shader is
        // shared and does not need the primary PC block at all (fragTint is already resolved by
        // the vertex stage).
        struct DualTextureDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture0;  ///< REMED-GFX-152: resolved once, at queue time
            SdlGpuSampledTextureEXT texture1;  ///< REMED-GFX-152: resolved once, at queue time
            ///@{ SDLGPU-21: independent per-slot sampler state (GraphicsDevice.SamplerStates[0]/[1]).
            int texture0Filter = 0;
            int texture0AddressU = 0;
            int texture0AddressV = 0;
            int texture0MaxAnisotropy = 4;  ///< REMED-GFX-170: captured with the filter
            int texture1Filter = 0;
            int texture1AddressU = 0;
            int texture1AddressV = 0;
            int texture1MaxAnisotropy = 4;  ///< REMED-GFX-170: captured with the filter
            ///@}
            bool hasVertexColor = false;  ///< stride 24 vs stride 20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // EnvironmentMapEffect (Phase SDLGPU-9, SDLGPU-33) -- stride 32 (VertexPositionNormalTexture,
        // same layout lit_textured3d uses). `envMapTexture` is resolved at Queue-time to the raw
        // SDL_GPUTexture* since GpuDrawParams::envMap (an ITextureCubeRenderer*) may be either a
        // plain SdlGpuTextureCubeRenderer or a SdlGpuRenderTargetCubeRenderer -- mirrors
        // SpriteBatch::Draw's own dual-renderer resolve for ITextureRenderer.
        struct EnvMapDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 24> uniforms{};       ///< PC: mvp(16) + diffuseColor(4) + emissiveAmount(4)
            std::array<float, 48> envMapUniforms{}; ///< EnvMapParams: world(16) + 8 vec4 (32) = 48 floats
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture;  ///< REMED-GFX-152: resolved once, at queue time
            SdlGpuSampledTextureEXT envMapTexture;  ///< REMED-GFX-152: resolved once, at queue time
            ///@{ REMED-GFX-173: the base 2D texture's own GraphicsDevice.SamplerStates[0].
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            /// REMED-GFX-170: captured with the filter, so a queued draw cannot observe a
            /// later ApplySamplerState. XNA SamplerState.MaxAnisotropy default.
            int maxAnisotropy = 4;
            ///@}
            ///@{ REMED-GFX-173: the reflection cube's own GraphicsDevice.SamplerStates[1], captured
            /// independently of slot 0 and by value -- exactly the shape DualTextureDrawCommand
            /// already uses for its second texture. Without these fields the cube's sampler could
            /// not survive to replay at all, whatever IssueEnvMapDraw bound.
            int envMapFilter = 0;
            int envMapAddressU = 0;
            int envMapAddressV = 0;
            int envMapMaxAnisotropy = 4;
            ///@}
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // SkinnedEffect (Phase SDLGPU-7, SDLGPU-34) -- stride 52 (VertexPositionNormalTextureSkinned)
        // or stride 56 (the same layout plus a per-vertex Color, `hasVertexColor` selects it).
        // Stride 52 draws reuse litTexturedFragmentShader_ unchanged (byte-identical varying
        // interface and UBO layout to lit_textured3d's own fragment shader). Stride 56 draws use a
        // dedicated skinnedColoredVertexShader_/skinnedColoredFragmentShader_ pair instead (see
        // skinned_colored3d.frag.glsl's own doc comment for why vertex color needs its own
        // fragment shader rather than folding into fragTint). The 72-bone palette (4608 bytes) is
        // uploaded as a real SDL_GPUBuffer (GRAPHICS_STORAGE_READ) and bound via
        // SDL_BindGPUVertexStorageBuffers, NOT pushed via SDL_PushGPUVertexUniformData --
        // empirically found (SdlGpu_Skinned) that this renderer's push-uniform-data mechanism has a
        // real ~4096-byte cap per slot on this Vulkan-backed environment, well under the full
        // 4608-byte palette.
        struct SkinnedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};        ///< PC: same 32-float layout FillExtUniforms already fills
            std::array<float, 72 * 16> boneUniforms{}; ///< BoneBlock: 72 mat4 = 1152 floats (4608 bytes), uploaded as a storage buffer
            std::array<float, 56> lightUniforms{};   ///< SkinnedLightParams: byte-identical to LitLightParams
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture;  ///< REMED-GFX-152: resolved once, at queue time
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            /// REMED-GFX-170: captured with the filter, so a queued draw cannot observe a
            /// later ApplySamplerState. XNA SamplerState.MaxAnisotropy default.
            int maxAnisotropy = 4;
            bool hasVertexColor = false;  ///< stride 56 vs stride 52
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
            SDL_GPUBuffer* uploadedBoneBuffer = nullptr;
        };

        // PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF) -- stride 48
        // (VertexPositionNormalTangentTexture, unskinned) or stride 68
        // (VertexPositionNormalTangentTextureSkinned), `skinned` selects the vertex shader/pipeline
        // (pbrVertexShader_/pbrSkinnedVertexShader_) and whether boneUniforms is uploaded/bound as
        // a storage buffer -- both variants share pbrFragmentShader_ unchanged (see
        // pbr_skinned3d.vert.glsl's own doc comment). uniforms/lightUniforms reuse FillExtUniforms/
        // FillLitLightUniforms's/FillSkinnedLightUniforms's existing layouts unchanged; pbrParams
        // is the one genuinely new uniform block (MetallicFactor/RoughnessFactor).
        struct PbrDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};          ///< PC (FillExtUniforms's existing layout)
            std::array<float, 56> lightUniforms{};     ///< LitLightParams/SkinnedLightParams (byte-identical)
            std::array<float, 4>  pbrParams{};          ///< PbrParams: metallicFactor, roughnessFactor, pad, pad
            bool skinned = false;
            std::array<float, 72 * 16> boneUniforms{}; ///< only used/uploaded when skinned == true
        std::array<float, 8> fogUniforms{};  ///< REMED-GFX-009 FogParams: vec4 fogColorEnabled + vec4 fogVector (32 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            SdlGpuSampledTextureEXT texture;                  ///< base color, required
            SdlGpuSampledTextureEXT normalMap;                ///< optional, default flat normal
            SdlGpuSampledTextureEXT metallicRoughnessMap;     ///< optional, default white
            SdlGpuSampledTextureEXT emissiveMap;              ///< optional, default white
            SdlGpuSampledTextureEXT occlusionMap;             ///< optional, default white
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            /// REMED-GFX-170: captured with the filter, so a queued draw cannot observe a
            /// later ApplySamplerState. XNA SamplerState.MaxAnisotropy default.
            int maxAnisotropy = 4;
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
            SDL_GPUBuffer* uploadedBoneBuffer = nullptr;  ///< only set when skinned == true
        };

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        /**
         * @brief plan_fx.md FX-071: one compiled-effect draw, deferred like every other draw kind.
         *
         * Unlike the eight stock-shader commands above, this one's shader pair, vertex attribute
         * set and uniform buffer bytes are captured at `Queue*Draw()` time rather than selected
         * from a fixed table at `Present()` -- see `SdlGpuCompiledEffect::LinkAndGetShadersEXT` and
         * `::CaptureUniformSnapshotEXT` for why this renderer's deferred model requires that.
         * Pixel-stage texture/sampler bindings are captured the same way, through
         * `SdlGpuCompiledEffect::GetBoundSamplerEXT`, and resolved eagerly to this renderer's own
         * sampleable handle so a later `Dispose()` of the source texture cannot invalidate an
         * already-queued draw (mirrors `SpriteCommand::texture`'s own `SdlGpuSampledTextureEXT`
         * precedent).
         *
         * First implementation scope: one vertex stream, pixel-stage 2D-texture sampling only. A
         * compiled effect outside that scope is refused when queued (`QueueCompiledEffectDraw`)
         * rather than silently drawing with an unbound or wrong-dimensionality sampler.
         */
        struct CompiledEffectDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;  ///< empty for a non-indexed draw
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            Uint32 firstIndex = 0;    ///< REMED-GFX-117: public startIndex, in index elements
            Sint32 vertexOffset = 0;  ///< REMED-GFX-117: public baseVertex, added once per index
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            Uint32 vertexStride = 0;
            std::vector<SDL_GPUVertexAttribute> vertexAttributes;
            /// Stable for this renderer's MojoShader context's whole lifetime (linker cache) --
            /// see LinkAndGetShadersEXT's own doc comment.
            SDL_GPUShader* vertexShader = nullptr;
            SDL_GPUShader* pixelShader = nullptr;
            std::vector<std::uint8_t> vertexUniformBytes;
            std::vector<std::uint8_t> pixelUniformBytes;

            /// One resolved sampler binding, in ascending slot order.
            struct SamplerBinding
            {
                SdlGpuSampledTextureEXT texture;
                int filter = 0;
                int addressU = 0;
                int addressV = 0;
                int maxAnisotropy = 4;
            };
            /// MOJOSHADER_sdlGetSamplerSlots(pixelShaderData) entries, one per slot in
            /// [0, that count) -- a slot the applied pass actually reflects a texture for holds the
            /// real resolved binding; every other slot (MojoShader reports at least one slot even
            /// for a shader that samples nothing at all -- see QueueCompiledEffectDraw's own doc
            /// comment) holds this renderer's default white texture so SDL_GPU's own binding-count
            /// validation, which checks the compiled shader module's declared count rather than
            /// MojoShader's reflected usage, is satisfied regardless.
            std::vector<SamplerBinding> pixelSamplers;
            /// MOJOSHADER_sdlGetSamplerSlots(vertexShaderData) dummy bindings (see pixelSamplers'
            /// own doc comment) -- always vertexDummyTexture, since this renderer's first
            /// implementation does not support a compiled effect's vertex shader sampling a texture
            /// at all (QueueCompiledEffectDraw refuses one that tries to).
            Uint32 vertexDummySamplerCount = 0;
            SdlGpuSampledTextureEXT vertexDummyTexture;  ///< resolved once at queue time; see above

            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;  ///< transient, set by UploadSceneDrawData
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;   ///< transient, set by UploadSceneDrawData
        };
#endif  // CNA_SDL_GPU_COMPILED_EFFECTS

        /**
         * @brief Constructs the renderer against an already-created SDL window.
         *
         * @param window SDL window to claim for `SDL_gpu` rendering. Must not be null.
         * @param virtualWidth Initial virtual (game-logic) resolution width.
         * @param virtualHeight Initial virtual (game-logic) resolution height.
         * @param presentationMode Initial presentation/scaling policy.
         * @param swapInterval Initial swap interval (0=immediate, 1=VSync, 2=half-rate).
         */
        SdlGpuRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                              CnaPresentationMode presentationMode, int swapInterval);
        /**
         * @brief Test-only constructor with scoped failure injection and destruction callbacks.
         * CNAEXT. Public renderer selection APIs continue to use the ordinary overload above.
         */
        CNAEXT SdlGpuRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                    CnaPresentationMode presentationMode, int swapInterval,
                                    const SdlGpuTestHooksEXT& testHooks);
        /** @brief Releases the window from the `SDL_GPUDevice` and destroys the device. */
        ~SdlGpuRenderer() override;

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        /**
         * @brief Parses a compiled XNA effect for this device (plan_fx.md FX-061).
         *
         * `SupportsCompiledEffects()` deliberately still reports false, so the public `Effect`
         * path never reaches this. It is reachable directly, which is how the runtime is tested
         * while the compiled-effect draw route is still missing.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeBytes Number of bytes at @p effectCode.
         * @return The runtime, or null if MojoShader has no context for this device.
         */
        std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* effectCode, std::size_t effectCodeBytes) override;

        /**
         * @brief CNAEXT. Returns this device's MojoShader context, creating it on first use.
         *
         * MojoShader allows one context per SDL_GPU device, so it is owned here rather than by
         * each effect.
         * @return The context, or null if it could not be created.
         */
        CNAEXT [[nodiscard]] MOJOSHADER_sdlContext* GetMojoShaderContextEXT();
#endif

        SdlGpuRenderer(const SdlGpuRenderer&) = delete;
        SdlGpuRenderer& operator=(const SdlGpuRenderer&) = delete;

        /** @brief Queues a color-only clear, consumed on the next render pass. */
        void Clear(float r, float g, float b, float a) override;
        /** @brief Renders any pending clear and presents the swapchain texture. */
        void Present() override;
        /** @brief Returns the current logical (virtual) viewport size. */
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Reads a region of the backbuffer into a tightly packed RGBA8 buffer (REMED-GFX-165).
         *
         * @param x       Left edge of the region, in backbuffer pixels.
         * @param y       Top edge of the region, in backbuffer pixels.
         * @param w       Region width in pixels.
         * @param h       Region height in pixels.
         * @param pixels  Destination for w*h RGBA8 texels (4 bytes each), top row first.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;
        /** @brief Updates the virtual (game-logic) resolution used for presentation scaling. */
        void SetVirtualResolution(int width, int height) override;
        /** @brief Updates the presentation/scaling policy. */
        void SetPresentationMode(int mode) override;
        /** @brief Updates the swap interval, reconfiguring the swapchain present mode. */
        void SetSwapInterval(int interval) override;
        /** @brief Converts a physical window point to logical (virtual) game coordinates. */
        bool TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const override;
        /** @brief Converts a logical (virtual) game point to physical window coordinates. */
        bool TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const override;

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /** @brief Queues a combined color+depth clear, consumed on the next render pass. */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /** @brief Queues a depth-only clear, consumed on the next render pass. */
        void ClearDepth(float depth) override;
        /** @brief Queues a stencil-only clear, consumed on the next render pass. */
        void ClearStencil(int stencil) override;
        /** @brief Queues a combined depth+stencil clear, consumed on the next render pass. */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /** @brief Queues a combined color+stencil clear, consumed on the next render pass. */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /** @brief Queues a combined color+depth+stencil clear, consumed on the next render pass. */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        /** @brief Stores the depth-test enabled flag, read by the 3D draw path's pipeline cache key. */
        void SetDepthTestEnabled(bool enabled) override { depthTestEnabled_ = enabled; }
        /** @brief Stores the blend-enabled flag directly -- a test-only shortcut separate from the
         * real `BlendState` object; `ApplyBlendState()` below is the real production path and may
         * overwrite this same field the next time a `BlendState` is assigned. */
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        /** @brief Stores the depth-write enabled flag, read by the 3D draw path's pipeline cache key. */
        void SetDepthWriteEnabled(bool enabled) override { depthWriteEnabled_ = enabled; }

        /**
         * @brief Real `BlendState` mapping (`SDLGPU-18`) -- stores the full requested blend
         * equation (not just enabled/disabled), baked into every 3D/sprite pipeline's
         * `SDL_GPUColorTargetBlendState` at `Queue*Draw()` time. Blend is considered "enabled"
         * unless the request is exactly `BlendState.Opaque`'s own values (`One`/`Zero`/`Add`),
         * mirroring `VulkanRenderer::ApplyBlendState`'s own convention.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        /**
         * @brief Real `DepthStencilState` mapping (`SDLGPU-19`) -- `depthEnable`/`depthWriteEnable`/
         * `depthFunc` update the same fields `SetDepthTestEnabled`/`SetDepthWriteEnabled` already
         * drive (whichever was set most recently wins, matching two setters sharing one field);
         * the stencil fields are new, baked into every 3D/sprite pipeline's front/back
         * `SDL_GPUStencilOpState`. `referenceStencil` is applied immediately and separately (real
         * render-pass-time state, not baked into the pipeline) via `SetReferenceStencil()`'s own
         * mechanism, matching `GraphicsDevice.ReferenceStencil`'s real standalone-property
         * semantics.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        /**
         * @brief Real `RasterizerState` mapping (`SDLGPU-20`) -- `cullMode`/`fillMode` are baked
         * into every 3D/sprite pipeline's `SDL_GPURasterizerState`; `scissorTestEnable` is captured
         * per draw and applied via `SDL_SetGPUScissor` (`REMED-GFX-068`, see `SetScissorRect()`).
         * `depthBias`/`slopeScaleDepthBias` are captured per queued draw and baked into
         * `SDL_GPURasterizerState` because SDL 3.5 exposes no dynamic depth-bias command
         * (REMED-GFX-051). Zero/signed-zero and non-polygon topologies normalize to disabled
         * native bias; triangle pipelines key the explicit enable bit and both factor values.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        /** @brief Stores `GraphicsDevice.BlendFactor` (the constant blend color used by the
         * `Blend::BlendFactor`/`Blend::InverseBlendFactor` modes), independent of a full
         * `BlendState` re-application -- matches `IGraphicsRenderer::SetBlendFactor`'s standalone
         * contract (`REMED-GFX-069`). The r/g/b/a are already normalized to `[0,1]` linear factors
         * by `GraphicsDevice::setBlendFactorProperty`. Store-only: captured PER DRAW into each
         * `QueuedDrawRef` at `Queue*Draw()`/`QueueSprite()` time (`PushDrawOrder`) and applied via
         * `SDL_SetGPUBlendConstants` per draw in `RenderQueuedDraws` (`ApplyBlendFactorForRef`).
         * Per-draw (like the viewport `REMED-GFX-064` and scissor `REMED-GFX-068`) because this
         * deferred renderer replays draws at Present, so a live-member read there would give every
         * queued draw the last BlendFactor set that frame -- the same deferred-model reasoning as
         * viewport/scissor, and the reason a per-pass read would be wrong. */
        void SetBlendFactor(float r, float g, float b, float a) override;
        /** @brief Stores `GraphicsDevice.ReferenceStencil`, independent of a full `DepthStencilState`
         * re-application -- matches `IGraphicsRenderer::SetReferenceStencil`'s own documented
         * standalone-property contract. Captured into `RenderStateSnapshot::stencilReference` at
         * the next `Queue*Draw()`/`QueueSprite()` call, then applied per draw call via a real
         * `SDL_SetGPUStencilReference` (a genuine SDL_gpu per-draw dynamic value, not baked into
         * the pipeline) in each `Render*Draws`/`RenderSprites` function. */
        void SetReferenceStencil(int value) override;
        /** @brief Real scissor-rect mapping (`SDLGPU-20`) -- stores the rect; captured PER DRAW into
         * each `QueuedDrawRef` at `Queue*Draw()`/`QueueSprite()` time (`PushDrawOrder`) alongside the
         * viewport, and applied via `SDL_SetGPUScissor` per draw in `RenderQueuedDraws`
         * (`ApplyScissorForRef`), gated on `ApplyRasterizerState`'s own `scissorTestEnable` flag (a
         * disabled scissor test uses the full render-target extents instead of this rect, matching
         * "no clipping"). Per-draw (not per-pass) for the same deferred-model reason as the viewport
         * (`REMED-GFX-068`, see `SetViewport()`): `SetRenderTarget` resets `ScissorRectangle` on
         * bind/unbind, so a per-pass read of the live scissor at Present would be the post-unbind
         * full-backbuffer rect, not the sub-rect an RT draw was issued under. */
        void SetScissorRect(int x, int y, int w, int h) override;
        /**
         * @brief Real `GraphicsDevice.Viewport` mapping (`REMED-GFX-064`) -- stores the sub-region
         * viewport rect + depth range; captured PER DRAW into each `QueuedDrawRef` at
         * `Queue*Draw()`/`QueueSprite()` time and applied via `SDL_SetGPUViewport` per draw in
         * `RenderQueuedDraws`. Per-draw (like the scissor, `REMED-GFX-068`) because this deferred
         * renderer replays draws at Present and `SetRenderTarget` resets the frame-global viewport
         * on bind/unbind, so a per-pass read of the live viewport would be wrong -- the same
         * deferred-model reasoning `VulkanRenderer` (GFX-062) uses. An unset or degenerate
         * (zero-size) viewport falls back to the pass's full render-target extents, byte-identical
         * to the pre-fix behavior (SDL's own default full-target viewport).
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        /**
         * @brief Real per-slot `SamplerState` mapping (`SDLGPU-21`) for direct 3D draws --
         * `SpriteBatch`'s own per-draw sampler selection (`SetSamplerFilter`/`SetSamplerAddressMode`)
         * already had its own separate path before this. Stores into `samplerSlots_[slot]`, read
         * directly into each `DrawCommand`'s own `textureFilter`/`addressU`/`addressV` fields at
         * the next `Queue*Draw()` call (slot 0 for every single-texture family; slots 0 and 1
         * independently for `DualTextureEffect`'s two texture units). `REMED-GFX-170`: since that
         * task, `maxAnisotropy` is captured into the command alongside the filter and genuinely
         * reaches `SDL_GPUSamplerCreateInfo::max_anisotropy` (only `TextureFilter::Anisotropic`
         * enables it), and it participates in the sampler cache key.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates an off-screen `RenderTarget2D` (Phase `SDLGPU-8`, `SDLGPU-35`/`SDLGPU-38`),
         * including real MSAA.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /** @brief Activates the given render target (pass nullptr to restore the swapchain). */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Creates a `RenderTargetCube` (Phase `SDLGPU-8`, `SDLGPU-36`), including real MSAA.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;

        /** @brief Creates a `Texture3D` (Phase `SDLGPU-9`, `SDLGPU-40`/`SDLGPU-41`). */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;

        /** @brief Creates a plain, uploaded `TextureCube` (Phase `SDLGPU-9`, `SDLGPU-51`). */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /**
         * @brief Creates a custom-`ShaderEffect` renderer (Phase `SDLGPU-10`, `SDLGPU-42`/`SDLGPU-43`),
         * compiling @p vertSrc/@p fragSrc (GLSL source) to SPIR-V at runtime via `libshaderc`.
         * Compilation failure is reported via the returned renderer's `IsValid()`/`GetCompileError()`
         * (matches `VulkanRenderer`/`DirectX11Renderer`'s own convention), not an
         * exception.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                            const std::string& fragSrc) override;

        /**
         * @brief Activates multiple render targets for MRT (Phase `SDLGPU-8`, `SDLGPU-37`).
         *
         * `rts[0]` becomes the real draw target (identical to `SetRenderTarget2D(rts[0])`);
         * `rts[1..count-1]` are bound and independently cleared but do not receive draws -- no
         * shader in this codebase declares more than one fragment output, the same honest scope
         * boundary this project's D3D11/D3D12 MRT support already established.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /** @brief Draws stride-16 (VertexPositionColor) primitives with a hardcoded white/vertex-color-enabled BasicEffect. */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        /** @brief Indexed counterpart of DrawColoredPrimitives(). */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /** @brief Effect-aware draw — dispatches to colored3d/textured3d/colored_textured3d/lit_textured3d by vertex stride. */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /** @brief Indexed counterpart of DrawPrimitivesEx(). */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Queues a sprite quad for drawing on the next render pass. CNAEXT — internal use
         * only. @p texture supplies GetWidth()/GetHeight() for UV math (works for any
         * ITextureRenderer); @p nativeTexture is the raw SDL_GPUTexture* actually bound at render
         * time (Texture2D and RenderTarget2D are unrelated concrete renderer classes, so
         * SdlGpuSpriteBatchRenderer::Draw resolves this once at call time).
         */
        CNAEXT void QueueSprite(const ITextureRenderer& texture, const SdlGpuSampledTextureEXT& nativeTexture,
                                const Rectangle& destinationRectangle,
                                const Rectangle& sourceRectangle,
                                const Color& color,
                                float rotation,
                                const Vector2& origin,
                                SpriteEffects effects,
                                float layerDepth,
                                const Matrix& transform,
                                int textureFilter,
                                int addressU,
                                int addressV,
                                SdlGpuEffectRenderer* customEffect = nullptr);

        /** @brief Returns the underlying `SDL_GPUDevice`. CNAEXT — internal use only. */
        CNAEXT [[nodiscard]] SDL_GPUDevice* Device() const { return device_; }
        /**
         * @brief Whether `SDL_CreateGPUDevice`'s `debug_mode` was requested for this device.
         *
         * Mirrors `DirectX11Renderer::IsDebugLayerEnabledEXT()`'s identical `#ifndef NDEBUG`
         * CNA-side toggle rationale (SDLGPU-6) — a debug build asks the Vulkan driver for
         * `SDL_gpu`'s own validation layer, a release build does not. CNAEXT.
         */
        CNAEXT [[nodiscard]] bool IsDebugModeEnabledEXT() const { return debugModeEnabled_; }
        /** @brief Number of cached stock SpriteBatch pipelines. Test-only compatibility/cache
         * cardinality introspection for REMED-GFX-097; target object and cube-face identity must
         * not increase this count. CNAEXT. */
        CNAEXT [[nodiscard]] std::size_t GetSpritePipelineCacheSizeEXT() const
        {
            return spritePipelines_.size();
        }
        /** @brief Number of cached stock colored-3D pipelines. Test-only pipeline-static
         * rasterizer-state introspection for REMED-GFX-051. CNAEXT. */
        CNAEXT [[nodiscard]] std::size_t GetColoredPipelineCacheSizeEXT() const
        {
            return coloredPipelines_.size();
        }
        /**
         * @brief Drives the ordinary lazy stock-sprite pipeline and sampler factories without
         * requiring swapchain presentation. Test-only GFX-028 failure/retry probe. CNAEXT.
         */
        CNAEXT void InitializeSpritePipelineAndSamplerForTestEXT();

    private:
        struct ConstructionResources;

        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        // Renders any pending clear into the acquired swapchain texture and submits the command
        // buffer. Returns false if no swapchain texture could be acquired this frame (minimized
        // window, lost surface, etc) -- mirrors WebGPURenderer::EnsureFrameRendered's
        // on-demand-submit semantics.
        bool EnsureFrameRendered();
        // (Re)creates depthStencilTexture_ if it does not already match the requested size.
        // depthStencilFormat_ itself is queried once in the constructor (QueryDepthStencilFormat),
        // not here, since pipeline creation needs a stable answer before any frame has rendered.
        void EnsureDepthStencilTexture(Uint32 width, Uint32 height);
        // Queries the best available combined depth+stencil format once, at construction time.
        static SDL_GPUTextureFormat QueryDepthStencilFormat(SDL_GPUDevice* device);
        static void ConfigureSwapchain(SDL_GPUDevice* device, SDL_Window* window, int interval);
        void MaybeFailForTest(SdlGpuFailurePointEXT point);
        void NotifyResourceEvent(SdlGpuResourceKindEXT resource,
                                 SdlGpuResourceEventEXT event) const noexcept;
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateGraphicsPipeline(
            const SDL_GPUGraphicsPipelineCreateInfo& createInfo, const char* diagnostic);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CacheGraphicsPipeline(
            std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*>& cache,
            std::size_t key, SDL_GPUGraphicsPipeline* pipeline);
        void ReleaseGraphicsPipeline(SDL_GPUGraphicsPipeline* pipeline) noexcept;
        void ReleaseShader(SDL_GPUShader*& shader) noexcept;
        void ReleaseSampler(SDL_GPUSampler*& sampler) noexcept;

        // Real architectural fix for a render-target-destroyed-before-flush use-after-free: a
        // RenderTarget2D/RenderTargetCube's destructor already purges itself from
        // the pending pass segments (so EnsureFrameRendered
        // never dereferences a dangling C++ object), but its OWN GPU texture handles must NOT be
        // released immediately -- some OTHER still-pending (not yet submitted) draw command may
        // sample this texture as an INPUT (SpriteBatch drawing this render target's contents
        // elsewhere, or EnvironmentMapEffect sampling it as a cube map) via a raw SDL_GPUTexture*
        // captured at Queue*Draw() time, independent of the pending pass segments entirely.
        // SDL_ReleaseGPUTexture() itself already defers the underlying GPU memory free until safe
        // (per SDL_gpu.h's own doc comment) -- but the HANDLE becomes invalid for any FURTHER API
        // call the instant it's released, so it must not be released while anything might still
        // reference it in a not-yet-submitted recording. Called from
        // SdlGpuRenderTargetRenderer/SdlGpuRenderTargetCubeRenderer's destructors (both are friends);
        // flushed in EnsureFrameRendered() right after a successful SDL_SubmitGPUCommandBuffer
        // (whatever was pending has now been handed to the GPU, so it's safe), and one final time
        // in ~SdlGpuRenderer() in case no further frame ever renders.
        void QueueTextureRelease(SDL_GPUTexture* texture);

        // sprite2d pipeline: shader modules, compatibility-keyed pipelines, and the renderer-wide
        // sampler cache, keyed by the COMPLETE description (filter, addressU, addressV,
        // maxAnisotropy) since REMED-GFX-170.
        // DepthStencilState.None disables tests/writes but does NOT erase pipeline target
        // metadata: REMED-GFX-097 therefore passes the active pass's actual depth format (or
        // INVALID for no attachment) into creation and cache selection.
        void CreateSpriteResources(ConstructionResources& resources);
        void DestroySpriteResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreateSpritePipeline(SDL_GPUTextureFormat colorFormat,
                                                                          SDL_GPUSampleCount sampleCount,
                                                                          SDL_GPUTextureFormat depthStencilFormat,
                                                                          int colorTargetCount,
                                                                          bool depthTest, bool depthWrite, int depthFunc,
                                                                          const RenderStateSnapshot& renderState);
        /**
         * @brief Resolves one complete public `SamplerState` to a cached native `SDL_GPUSampler*`.
         *
         * REMED-GFX-170: this is the renderer's ONE sampler translation, used by SpriteBatch and by
         * every 3D family alike. It used to resolve `textureFilter == 0 ? LINEAR : NEAREST` and to
         * key an 18-entry array on `filter == 0 ? 0 : 1`, so both the descriptor and the cache key
         * collapsed all eight non-`Linear` ordinals onto Point.
         *
         * @param textureFilter Raw XNA `TextureFilter` ordinal (0..8).
         * @param addressU Raw `TextureAddressMode` ordinal for U (0=Wrap, 1=Clamp, 2=Mirror).
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Public `SamplerState.MaxAnisotropy`, clamped to 1..16; applied only
         *        for `TextureFilter::Anisotropic`, but always part of the cache key.
         * @param family Public draw family, for `CNA_SDLGPU_SAMPLER_TRACE` only.
         * @return The cached or newly created native sampler; never null.
         */
        [[nodiscard]] SDL_GPUSampler* GetOrCreateSampler(int textureFilter, int addressU,
                                                         int addressV, int maxAnisotropy,
                                                         const char* family);
        // Uploads all queued sprite vertex data (copy pass) -- must run BEFORE
        // BeginGPURenderPass; SDL_gpu forbids a copy pass nested inside a render pass.
        void UploadSpriteVertexData(SDL_GPUCommandBuffer* cmd);
        // Issues the actual bind+draw calls for ONE queued sprite -- called from
        // RenderQueuedDraws() in real chronological (drawOrder_) order, not grouped with every
        // other sprite (adversarial-review finding #4: draw ordering). @p index is this sprite's
        // own position in spriteCommands_, needed for its vertex-buffer offset.
        // colorTargetCount > 1 (real MRT, SDLGPU-37) is forwarded to a customEffect's own
        // GetOrCreatePipeline() so a real multi-output fragment shader can build a pipeline
        // matching this pass's actual attachment count -- stock (single-output) sprites are
        // unaffected, since GetOrCreateSpritePipeline() always builds exactly 1 color target.
        void IssueSpriteDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const SpriteCommand& command,
                             std::size_t index, const float* viewportSize, SDL_GPUTextureFormat colorFormat,
                             SDL_GPUSampleCount sampleCount, SDL_GPUTextureFormat depthStencilFormat,
                             int colorTargetCount,
                             SDL_GPUGraphicsPipeline*& boundPipeline);

        // Phase SDLGPU-6: colored3d/textured3d/colored_textured3d/lit_textured3d.
        void CreateColoredResources(ConstructionResources& resources);
        void DestroyColoredResources();
        void CreateTexturedResources(ConstructionResources& resources);   ///< also creates colored_textured3d's vertex shader (shares textured3d's fragment shader)
        void DestroyTexturedResources();
        void CreateLitTexturedResources(ConstructionResources& resources);
        void DestroyLitTexturedResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColored3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColoredTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineLitTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        // params == nullptr: the legacy DrawColoredPrimitives path (hardcoded white diffuse,
        // vertexColorEnabled=true). params != nullptr: DrawPrimitivesEx's real GpuDrawParams path.
        void QueueColoredDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams* params = nullptr);
        void QueueTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                               const Matrix& world, const Matrix& view, const Matrix& projection,
                               PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void QueueLitTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);

        // Phase SDLGPU-7: AlphaTestEffect / DualTextureEffect.
        void CreateAlphaTestResources(ConstructionResources& resources);
        void DestroyAlphaTestResources();
        void CreateDualTextureResources(ConstructionResources& resources);
        void DestroyDualTextureResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineAlphaTest3D(
            std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineDualTexture3D(
            std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        void QueueAlphaTestDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void QueueDualTextureDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssueAlphaTestDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const AlphaTestDrawCommand& command,
                               SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                               SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                               SDL_GPUGraphicsPipeline*& boundPipeline);
        void IssueDualTextureDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const DualTextureDrawCommand& command,
                                 SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                                 SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                 SDL_GPUGraphicsPipeline*& boundPipeline);

        // Phase SDLGPU-9: EnvironmentMapEffect (SDLGPU-33).
        void CreateEnvMapResources(ConstructionResources& resources);
        void DestroyEnvMapResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineEnvMap3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        void QueueEnvMapDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssueEnvMapDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const EnvMapDrawCommand& command,
                            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                            SDL_GPUGraphicsPipeline*& boundPipeline);

        // Phase SDLGPU-7: SkinnedEffect (SDLGPU-34). GetOrCreatePipelineSkinned3D's `hasVertexColor`
        // selects the stride-56 skinnedColoredVertexShader_/skinnedColoredFragmentShader_ pair
        // instead of the stride-52 skinnedVertexShader_/litTexturedFragmentShader_ pair.
        void CreateSkinnedResources(ConstructionResources& resources);
        void DestroySkinnedResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineSkinned3D(
            bool hasVertexColor, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        void QueueSkinnedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssueSkinnedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const SkinnedDrawCommand& command,
                             SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                             SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                             SDL_GPUGraphicsPipeline*& boundPipeline);

        // PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF). `skinned` selects
        // pbrVertexShader_/pbrSkinnedVertexShader_ (both share pbrFragmentShader_ unchanged).
        // EnsureDefaultPbrTextures() lazily creates the 1x1 fallback textures the 4 optional maps
        // bind when the effect leaves them unset, mirroring EasyGLRenderer::
        // EnsureDefaultWhiteTexture()/EnsureDefaultFlatNormalTexture()'s identical role.
        void CreatePbrResources(ConstructionResources& resources);
        void DestroyPbrResources();
        void EnsureDefaultPbrTextures();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelinePbr3D(
            bool skinned, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState);
        void QueuePbrDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssuePbrDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const PbrDrawCommand& command,
                         SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                         SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                         SDL_GPUGraphicsPipeline*& boundPipeline);

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-071: compiled-effect draw route. Unlike every stock family above, there is
        // no fixed shader/pipeline table -- the pipeline is keyed on the applied pass's own linked
        // shader pair, vertex layout and render state, all captured at queue time (see
        // CompiledEffectDrawCommand's own doc comment for why).
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineCompiledEffect(
            const CompiledEffectDrawCommand& command,
            SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount);
        void QueueCompiledEffectDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params);
        void IssueCompiledEffectDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                    const CompiledEffectDrawCommand& command,
                                    SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                                    SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                    SDL_GPUGraphicsPipeline*& boundPipeline);
#endif

        // Uploads every queued 3D draw command's shadow-copied vertex/index data into a fresh
        // transient SDL_GPUBuffer per command (mirrors WebGPURenderer's own per-draw
        // transient-buffer approach) -- must run in the same copy pass as UploadSpriteVertexData,
        // BEFORE BeginGPURenderPass.
        void UploadSceneDrawData(SDL_GPUCommandBuffer* cmd);
        void IssueColoredDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ColoredDrawCommand& command,
                             SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                             SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                             SDL_GPUGraphicsPipeline*& boundPipeline);
        void IssueTexturedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const TexturedDrawCommand& command,
                              SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                              SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                              SDL_GPUGraphicsPipeline*& boundPipeline);
        void IssueLitTexturedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const LitTexturedDrawCommand& command,
                                 SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                                 SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                 SDL_GPUGraphicsPipeline*& boundPipeline);
        // Adversarial-review finding #4 (draw ordering): replaces the old fixed
        // "all Colored3D, then all Textured3D, ... then all Sprites" sequence with a single pass
        // over drawOrder_ (real chronological Queue*Draw()/QueueSprite() issue order), dispatching
        // each ref to its own Issue*Draw() function. A target/kind that isn't ready yet (no
        // uploaded vertex buffer, missing texture, etc.) is skipped exactly like the old per-family
        // loops used to skip it -- only the ORDER changed, not the readiness rules. boundPipeline
        // is now tracked globally across every kind, not just within one family, so consecutive
        // same-pipeline draws of DIFFERENT kinds also skip a redundant rebind.
        // REMED-GFX-145/143: @p segment restricts the replay to the ONE bind cycle this pass
        // represents, and that now includes a backbuffer cycle. The old "replay every draw whose
        // DrawTarget is the swapchain, whichever cycle it was issued in" escape is gone with the
        // single trailing swapchain pass it existed for.
        void RenderQueuedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const DrawTarget& target,
                              SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                              SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                              std::uint64_t segment);
        // Releases every transient buffer UploadSceneDrawData created, and clears all 3 queues --
        // safe to call immediately after SDL_SubmitGPUCommandBuffer (SDL_gpu defers the actual
        // free until the GPU is done, per SDL_ReleaseGPUBuffer's own documented contract).
        void ReleaseSceneDrawBuffers(bool clearCommands = true);
        [[nodiscard]] SDL_GPUPrimitiveType ToTopology(PrimitiveType primitive) const;
        [[nodiscard]] int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const;
        [[nodiscard]] int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const;

        // REMED-GFX-117: the single place the public indexed draw range becomes native
        // SDL_DrawGPUIndexedPrimitives arguments. Every one of this renderer's eight indexed
        // pipeline families routes through it, so no family can silently fall back to a
        // default-zero offset again. Validates the requested range against the bound buffers and
        // against the native argument types before any conversion, and throws rather than clamping.
        [[nodiscard]] NativeIndexedRange ResolveIndexedRange(
            const SdlGpuIndexBufferRenderer& ib, const SdlGpuVertexBufferRenderer& vb,
            PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params) const;

        // Convenience wrapper: resolves the range once and copies it onto whichever draw-command
        // family is being queued. `params` is null only for the DrawUser* path, whose vertex and
        // index data GraphicsDevice has already copied and rebased, so its native offsets are
        // legitimately zero.
        template<typename CommandT>
        void ApplyIndexedRange(CommandT& command,
                               const SdlGpuIndexBufferRenderer& ib,
                               const SdlGpuVertexBufferRenderer& vb,
                               PrimitiveType primitive, int primitiveCount,
                               const GpuDrawParams* params) const
        {
            const NativeIndexedRange range =
                ResolveIndexedRange(ib, vb, primitive, primitiveCount, params);
            command.indexCount = range.indexCount;
            command.firstIndex = range.firstIndex;
            command.vertexOffset = range.vertexOffset;
        }

        // Phase SDLGPU-8: renders ONE off-screen render-target segment's own pass (the draws and
        // sprites queued inside that one bind cycle, across every shader family) and regenerates
        // its mip chain afterward if requested. REMED-GFX-145: called once per SEGMENT, not once
        // per distinct target -- all before the swapchain pass itself.
        // REMED-GFX-156: @p colorLoadedLater says a later segment of this frame loads this
        // resource's colour again, which upgrades a multisampled RESOLVE to RESOLVE_AND_STORE.
        void RenderToTarget(SDL_GPUCommandBuffer* cmd, const PassSegment& segment,
                            bool colorLoadedLater);
        // Phase SDLGPU-8 (SDLGPU-36): renders ONE RenderTargetCube-face segment's own pass.
        // REMED-GFX-145: once per bind cycle of that face, not once per distinct (cube, face).
        void RenderToTargetCubeFace(SDL_GPUCommandBuffer* cmd, const PassSegment& segment,
                                    bool colorLoadedLater);
        // REMED-GFX-145: opens a new logical render-pass segment for whatever was just bound, and
        // makes it the segment every subsequent Queue*Draw()/QueueSprite()/Clear() belongs to.
        // Called from EVERY bind site, including a rebind of the target that is already current --
        // that is precisely the case the old target-identity grouping could not represent.
        void BeginRenderTargetSegment(const std::shared_ptr<SdlGpuRenderTarget2DState>& rt);
        void BeginCubeFaceSegment(const std::shared_ptr<SdlGpuRenderTargetCubeState>& cube, int face);
        /**
         * @brief REMED-GFX-143: opens a BACKBUFFER bind cycle.
         *
         * Called wherever the backbuffer becomes the active target -- at construction, on every
         * unbind of a render target or cube face, and when a mid-frame flush rebuilds the segment
         * list -- so leaving the backbuffer closes its cycle and returning opens a NEW one. Marking
         * the frame pending is deliberately left to whatever is queued next: merely selecting the
         * backbuffer is not work.
         */
        void BeginBackbufferSegment();
        /**
         * @brief Records one backbuffer bind cycle into the acquired swapchain texture.
         *
         * @param cmd               The frame's command buffer.
         * @param segment           The bind cycle to replay.
         * @param swapchainTexture  The texture acquired once for this frame.
         * @param isFirstBackbuffer This is the frame's first backbuffer pass, so a segment with no
         *                          Clear() of its own keeps the pre-fix load action.
         */
        void RenderToSwapchain(SDL_GPUCommandBuffer* cmd, const PassSegment& segment,
                               SDL_GPUTexture* swapchainTexture, bool isFirstBackbuffer);
        /**
         * @brief REMED-GFX-165: (re)creates the readback proxy at @p width x @p height in the current
         *        swapchain format when it does not already match, returning true when a usable proxy
         *        exists afterwards.
         */
        bool EnsureBackbufferProxy(Uint32 width, Uint32 height);
        /// REMED-GFX-165: copies the readback proxy into the acquired swapchain texture for present.
        void BlitBackbufferProxyToSwapchain(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchainTexture);
        /// Closes the open segment; subsequent work belongs to the swapchain until the next bind.
        void EndRenderTargetSegment();
        /// The open segment, or null when the swapchain is current.
        [[nodiscard]] PassSegment* CurrentSegment();
        /**
         * @brief REMED-GFX-156: the segment one public Clear() must be recorded into.
         *
         * SDL_gpu delivers a clear only through SDL_GPUColorTargetInfo.load_op / the depth-stencil
         * target's load ops, and a render pass has exactly one set of those, so a Clear() recorded
         * into a segment that has already drawn would run BEFORE that draw. When the open segment
         * has drawn, this closes it and opens another one over the SAME destination -- same render
         * target, same cube face, same MRT attachment set -- so the clear becomes that new pass's
         * load action and every later draw of the bind cycle joins it. With nothing drawn since the
         * segment opened there is nothing to separate, so the request folds into the open segment
         * and no extra pass is created: a leading Clear(), and any run of Clear()s not interrupted
         * by a draw, still costs exactly one pass.
         *
         * @return The segment to record the request into, or null while no segment is open at all.
         */
        [[nodiscard]] PassSegment* SegmentForOrderedClear();
        /**
         * @brief REMED-GFX-156: does a LATER segment of this frame render into @p segment's colour
         *        resource again?
         *
         * Only interesting while that resource is multisampled: SDL_GPU_STOREOP_RESOLVE explicitly
         * leaves the multisample contents undefined, so a following segment that LOADs them (a
         * depth-only or stencil-only ordered Clear, which must preserve colour) would read garbage.
         * The answer selects SDL_GPU_STOREOP_RESOLVE_AND_STORE for exactly those segments, which is
         * the same correction REMED-GFX-141 already applied to preserving cube faces.
         *
         * @param index Position of the segment in @ref passSegments_.
         * @return True when some later segment names the same 2D target or the same cube face.
         */
        [[nodiscard]] bool SegmentColorLoadedLater(std::size_t index) const;
        /**
         * @brief REMED-GFX-156: names @p segment's destination for the native order trace.
         *
         * @param segment The segment to name.
         * @return "backbuffer", "rendertarget2d" or "rendertargetcube-faceN", owned by the renderer.
         */
        [[nodiscard]] static const char* SegmentDestinationName(const PassSegment& segment);
        /**
         * @brief REMED-GFX-156: writes one native order-trace line for a recorded pass.
         *
         * The trace exists because a final image cannot tell an ordered clear apart from a load-op
         * clear that happened to land on the same pixels: it reports the public command stream, the
         * logical segment each command belongs to, the native pass each segment became, and the
         * load/store action every attachment of that pass was given.
         *
         * @param passIndex   Native pass number within this frame, in recording order.
         * @param segment     The segment being recorded.
         * @param colorLoad   Colour load action name.
         * @param colorStore  Colour store action name.
         * @param depthLoad   Depth load action name, or "none" without a depth attachment.
         * @param stencilLoad Stencil load action name, or "none" without a depth attachment.
         * @param draws       Number of queued draws this pass will issue.
         */
        void TracePassSegment(std::size_t passIndex, const PassSegment& segment,
                              const char* colorLoad, const char* colorStore,
                              const char* depthLoad, const char* stencilLoad,
                              std::size_t draws) const;
        /// Re-opens a segment for whatever is still bound after a mid-frame flush cleared the
        /// segment list, so draws issued after that flush are not silently dropped.
        void ReopenSegmentForBoundTarget();
        /// True when a resource this segment names has never been written by any pass, so this
        /// segment must run even with no draws and no explicit Clear() -- otherwise the texture
        /// stays uninitialized and a later LOAD reads garbage.
        [[nodiscard]] static bool SegmentOwesFirstUseClear(const PassSegment& segment);
        // Returns the DrawTarget matching whichever target (swapchain/2D RT/cube face) is
        // currently bound -- 2D and cube binding are mutually exclusive (see
        // SdlGpuRenderTargetRenderer::BindAsRenderTarget/SdlGpuRenderTargetCubeRenderer::BindAsRenderTargetFace).
        [[nodiscard]] DrawTarget CurrentDrawTarget() const;
        // SDLGPU-18/19/20: snapshots the renderer's current blend/cull/fillmode/stencil state --
        // called once per Queue*Draw()/QueueSprite() so later ApplyBlendState/ApplyRasterizerState/
        // ApplyDepthStencilState calls never retroactively change an already-queued draw's baked
        // pipeline (mirrors every other per-command uniform snapshot already established here).
        [[nodiscard]] RenderStateSnapshot CaptureRenderState() const;
        // REMED-GFX-068: appends a QueuedDrawRef, snapshotting the current GraphicsDevice.Viewport
        // (viewportSet_/viewportX_/...) AND the current RasterizerState.ScissorTestEnable +
        // GraphicsDevice.ScissorRectangle (scissorEnabled_/scissorX_/...) into it, so each queued
        // draw carries the viewport and scissor it was issued under (per-draw capture, needed for
        // this deferred renderer -- see SetViewport()/SetScissorRect()).
        void PushDrawOrder(DrawKind kind, std::size_t index);
        // REMED-GFX-064: applies SDL_SetGPUViewport for one queued draw, using the ref's captured
        // viewport if set, otherwise the pass's full render-target extents (byte-identical to the
        // pre-fix implicit full-target viewport). Called per draw from RenderQueuedDraws.
        void ApplyViewportForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref,
                                 int targetWidth, int targetHeight) const;
        // REMED-GFX-068: applies SDL_SetGPUScissor for one queued draw, using the ref's captured
        // scissor rect (clamped to the target's physical extent) if the scissor test was enabled,
        // otherwise the pass's full render-target extents (no clip). Called per draw from
        // RenderQueuedDraws, mirroring ApplyViewportForRef.
        void ApplyScissorForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref,
                                int targetWidth, int targetHeight) const;
        // REMED-GFX-069: applies SDL_SetGPUBlendConstants for one queued draw, using the ref's
        // captured GraphicsDevice.BlendFactor. Applied unconditionally (like the viewport/scissor):
        // it is inert for pipelines that don't reference CONSTANT_COLOR/ONE_MINUS_CONSTANT_COLOR, so
        // no gating is needed. No target extents needed (a color factor, not a rect). Called per
        // draw from RenderQueuedDraws, mirroring ApplyViewportForRef/ApplyScissorForRef.
        void ApplyBlendFactorForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref) const;

        SDL_Window* window_ = nullptr;
        SDL_GPUDevice* device_ = nullptr;
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // One MojoShader context per SDL_GPU device, created on first compiled effect.
        MOJOSHADER_sdlContext* mojoShaderContext_ = nullptr;
#endif
        SdlGpuTestHooksEXT testHooks_{};
        bool testFailureInjected_ = false;
        bool registeredForWindow_ = false;
        SDL_GPUTexture* depthStencilTexture_ = nullptr;
        SDL_GPUTextureFormat depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;

        // REMED-GFX-165: the swapchain texture is write-only by permanent SDL contract
        // (SDL_WaitAndAcquireGPUSwapchainTexture cannot be a sampler/copy/blit SOURCE), so the
        // backbuffer cannot be read back directly. Once GetBackBufferData is first called, the
        // backbuffer pass renders into this self-owned SAMPLER|COLOR_TARGET proxy instead of straight
        // into the swapchain texture, and one SDL_BlitGPUTexture(proxy -> swapchain) presents it;
        // ReadBackbuffer then downloads from the proxy through the same transfer-buffer+fence path the
        // render targets already use. Lazily created on the first read, so a game that never reads the
        // backbuffer pays NOTHING (this resolves plan_sdlgpu.md SDLGPU-39's per-frame-cost objection --
        // the deferred-frame model means the first read happens while the frame is still pending, so
        // the proxy genuinely can be allocated on demand rather than "before every frame's draws").
        SDL_GPUTexture* backbufferProxy_ = nullptr;
        int backbufferProxyWidth_ = 0;
        int backbufferProxyHeight_ = 0;
        SDL_GPUTextureFormat backbufferProxyFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        bool backbufferReadbackEnabled_ = false;
        /// SDLGPU-6: whether SDL_CreateGPUDevice's debug_mode was requested (an #ifndef NDEBUG
        /// CNA-side toggle, mirroring DirectX11Renderer::debugLayerEnabled_'s identical rationale).
        bool debugModeEnabled_ = false;

        SDL_GPUShader* spriteVertexShader_ = nullptr;
        SDL_GPUShader* spriteFragmentShader_ = nullptr;
        // Keyed by (int)colorFormat -- Phase SDLGPU-8 needs more than one variant (swapchain
        // format vs. render-target R8G8B8A8_UNORM), unlike Phases 1-7 where sprites only ever
        // targeted the swapchain.
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> spritePipelines_;
        /// REMED-GFX-170: keyed on the complete sampler description (filter | addressU | addressV |
        /// maxAnisotropy), so two states that differ in ANY component get their own native sampler.
        std::unordered_map<std::uint32_t, SDL_GPUSampler*> samplerCache_;
        SDL_GPUBuffer* spriteVertexBuffer_ = nullptr;
        Uint32 spriteVertexCapacityBytes_ = 0;
        std::vector<SpriteCommand> spriteCommands_;
        Uint32 depthStencilWidth_ = 0;
        Uint32 depthStencilHeight_ = 0;

        // Adversarial-review finding #4 (draw ordering): one entry per Queue*Draw()/QueueSprite()
        // call, in REAL chronological issue order -- no sort needed, since append-at-call-time
        // already is that order. RenderQueuedDraws() replays this once per render pass instead of
        // the old fixed "all Colored3D, then all Textured3D, ... then all Sprites" sequence, so a
        // game that interleaves 3D and SpriteBatch draws within one frame gets correct alpha-blend
        // layering between them. Cleared alongside the 7 family vectors + spriteCommands_ each
        // frame (ReleaseSceneDrawBuffers()/EnsureFrameRendered()).
        std::vector<QueuedDrawRef> drawOrder_;

        // Phase SDLGPU-6 pipeline caches, keyed by topology*4+depthTest*2+depthWrite (depthFunc
        // is folded in as topology*4*8+depthFunc*4+... -- see the .cpp's PipelineCacheKey() for
        // the exact packing, mirroring WebGPURenderer's own int-keyed cache convention).
        SDL_GPUShader* coloredVertexShader_ = nullptr;
        SDL_GPUShader* coloredFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> coloredPipelines_;
        std::vector<ColoredDrawCommand> coloredDrawCommands_;

        SDL_GPUShader* texturedVertexShader_ = nullptr;         ///< stride 20 (VertexPositionTexture)
        SDL_GPUShader* coloredTexturedVertexShader_ = nullptr;  ///< stride 24 (VertexPositionColorTexture)
        SDL_GPUShader* texturedFragmentShader_ = nullptr;       ///< shared by both stride-20/24 pipelines
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> texturedPipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> coloredTexturedPipelines_;
        std::vector<TexturedDrawCommand> texturedDrawCommands_;

        SDL_GPUShader* litTexturedVertexShader_ = nullptr;
        SDL_GPUShader* litTexturedFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> litTexturedPipelines_;
        std::vector<LitTexturedDrawCommand> litTexturedDrawCommands_;

        // Phase SDLGPU-7. alphaTestPipelines_ holds BOTH stride-20 and stride-32 pipelines
        // (shared shader, different vertex_input_state) -- its cache key folds in the stride
        // (see GetOrCreatePipelineAlphaTest3D's own key computation), unlike every other
        // pipeline map here which is already stride-specific by construction.
        SDL_GPUShader* alphaTestVertexShader_ = nullptr;         ///< strides 20/32 (no vertex colour)
        SDL_GPUShader* alphaTestColoredVertexShader_ = nullptr;  ///< stride 24 (vertex colour tint)
        SDL_GPUShader* alphaTestFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> alphaTestPipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> alphaTestColoredPipelines_;
        std::vector<AlphaTestDrawCommand> alphaTestDrawCommands_;

        SDL_GPUShader* dualTextureVertexShader_ = nullptr;         ///< stride 20
        SDL_GPUShader* dualTextureColoredVertexShader_ = nullptr;  ///< stride 24
        SDL_GPUShader* dualTextureFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> dualTexturePipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> dualTextureColoredPipelines_;
        std::vector<DualTextureDrawCommand> dualTextureDrawCommands_;

        SDL_GPUShader* envMapVertexShader_ = nullptr;
        SDL_GPUShader* envMapFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> envMapPipelines_;
        std::vector<EnvMapDrawCommand> envMapDrawCommands_;

        // No dedicated fragment shader for stride 52 -- reuses litTexturedFragmentShader_ (see
        // SkinnedDrawCommand's own doc comment). Stride 56 (vertex color) uses its own dedicated
        // pair below, cached separately from skinnedPipelines_ (mirrors alphaTestPipelines_/
        // alphaTestColoredPipelines_'s own separate-map-per-stride convention).
        SDL_GPUShader* skinnedVertexShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> skinnedPipelines_;
        std::vector<SkinnedDrawCommand> skinnedDrawCommands_;

        SDL_GPUShader* skinnedColoredVertexShader_ = nullptr;    ///< stride 56
        SDL_GPUShader* skinnedColoredFragmentShader_ = nullptr;  ///< stride 56 (see skinned_colored3d.frag.glsl)
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> skinnedColoredPipelines_;

        // PbrEffect/SkinnedPbrEffect. pbrFragmentShader_ is shared by both the unskinned
        // (pbrVertexShader_, stride 48) and skinned (pbrSkinnedVertexShader_, stride 68)
        // pipelines -- cached separately since their vertex_input_state/vertex shader differ.
        SDL_GPUShader* pbrVertexShader_ = nullptr;
        SDL_GPUShader* pbrSkinnedVertexShader_ = nullptr;
        SDL_GPUShader* pbrFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> pbrPipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> pbrSkinnedPipelines_;
        std::vector<PbrDrawCommand> pbrDrawCommands_;
        // 1x1 fallback textures for PbrEffect's 4 optional maps when left unbound -- lazily
        // created by EnsureDefaultPbrTextures(). default_white_ makes an absent metallic-
        // roughness/emissive/occlusion map read as "factor alone"/"no emissive tint"/"fully lit"
        // (each semantic's own neutral value is 1.0); default_flat_normal_ makes an absent normal
        // map decode (via the shader's rgb*2-1) to the unperturbed geometric normal (0,0,1).
        // Mirrors EasyGLRenderer::default_white_texture_/default_flat_normal_texture_.
        std::unique_ptr<SdlGpuTextureRenderer> defaultWhiteTexture_;
        std::unique_ptr<SdlGpuTextureRenderer> defaultFlatNormalTexture_;

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-071: unlike every stock family's cache above, this one is keyed on a
        // linked shader pair rather than a fixed shader field, since arbitrary compiled effects
        // share it across effect instances (see GetOrCreatePipelineCompiledEffect).
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> compiledEffectPipelines_;
        std::vector<CompiledEffectDrawCommand> compiledEffectDrawCommands_;
#endif

        int depthCompareFunction_ = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual (DepthStencilState.Default)

        // Phase SDLGPU-8: nullptr = the swapchain is the active target. REMED-GFX-145: which pass a
        // draw belongs to is decided by its SEGMENT (passSegments_ below), not by this pointer --
        // this only answers "what is bound right now" for CurrentDrawTarget()/Clear() routing.
        SdlGpuRenderTargetRenderer* currentRenderTarget_ = nullptr;

        // See QueueTextureRelease's own doc comment -- GPU texture handles from a destroyed
        // render target, deferred until the next successful command-buffer submit.
        std::vector<SDL_GPUTexture*> pendingTextureReleases_;

        // SDLGPU-36: mirrors currentRenderTarget_ for RenderTargetCube faces -- currentRenderTarget_
        // and currentRenderTargetCube_ are mutually exclusive (binding one clears the other,
        // matching SetRenderTarget2D/SetRenderTargetCubeFace's real XNA single-current-target
        // semantics).
        SdlGpuRenderTargetCubeRenderer* currentRenderTargetCube_ = nullptr;
        int currentActiveCubeFace_ = -1;

        // REMED-GFX-145: every public render-target bind cycle of this frame, in the exact order
        // the cycles were opened. Holds the owning shared_ptr, so a target's GPU state stays alive
        // for its own pending Clear()/draws even if the wrapper is destroyed before Present() (see
        // SdlGpuRenderTarget2DState's own doc comment). Cleared, like every other queue, right
        // after a successful submit.
        std::vector<PassSegment> passSegments_;
        /**
         * The open segment's id. REMED-GFX-143: never `kSwapchainSegment` in ordinary operation --
         * the backbuffer owns real segments too, so a draw issued with no render target bound
         * carries the id of the backbuffer cycle it was issued in and is replayed in exactly that
         * cycle's pass. The sentinel survives only as the "no segment has ever been opened" value.
         */
        std::uint64_t currentSegment_ = kSwapchainSegment;
        /// Monotonically increasing; never reused within a frame, never a cache key.
        std::uint64_t nextSegmentId_ = 1;
        /**
         * @brief REMED-GFX-156: native render passes recorded so far in the frame being flushed.
         *
         * Reset at the start of each flush and incremented by every SDL_BeginGPURenderPass this
         * renderer issues, so the order trace can state which native pass a logical segment became
         * and the pass-cardinality claims can be read off the trace rather than assumed.
         */
        std::size_t nativePassIndex_ = 0;

        int physicalWidth_ = 0;
        int physicalHeight_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;

        bool framePending_ = true;

        bool clearColorPending_ = true;
        bool clearDepthPending_ = true;
        bool clearStencilPending_ = false;
        SDL_FColor clearColor_{0.0f, 0.0f, 0.0f, 1.0f};
        float clearDepth_ = 1.0f;
        Uint8 clearStencil_ = 0;

        bool depthTestEnabled_ = false;
        bool depthWriteEnabled_ = false;
        bool blendEnabled_ = false;  ///< matches BlendState.Opaque (no blend), the real XNA default

        // SDLGPU-18/19/20: "current" render state, mirroring VulkanRenderer's own
        // blendParams_/dsParams_/cullMode_ pattern for a renderer whose pipeline objects are
        // pre-baked/immutable, not a live pipeline-state-object mechanism. Captured into a
        // RenderStateSnapshot at Queue*Draw()/QueueSprite() time (see CaptureRenderState()).
        BlendKeyParams blendParams_;
        std::array<int, 4> colorWriteMasks_{{15, 15, 15, 15}};  ///< REMED-GFX-077/-098: current per-MRT-slot masks
        int cullMode_ = 2;         ///< XNA CullMode ordinal; 2 = CullCounterClockwiseFace (RasterizerState's real default)
        bool fillModeWireframe_ = false;
        StencilKeyParams stencilParams_;  ///< readMask/writeMask live here now, see StencilKeyParams's own doc comment
        int referenceStencil_ = 0;  ///< real render-pass-time state (SDL_SetGPUStencilReference), not baked into any pipeline
        // REMED-GFX-069: current GraphicsDevice.BlendFactor (normalized [0,1] linear factors), set by
        // SetBlendFactor() and snapshotted into each QueuedDrawRef at Queue*Draw()/QueueSprite() time
        // (PushDrawOrder), then applied via SDL_SetGPUBlendConstants per draw (ApplyBlendFactorForRef).
        // Default 1,1,1,1 = XNA's GraphicsDevice.BlendFactor default (Color::White); this differs from
        // SDL_gpu's own uninitialized blend constant, so pre-SetBlendFactor constant-color blends get
        // the correct XNA default rather than a driver-dependent value.
        float blendFactorR_ = 1.0f;
        float blendFactorG_ = 1.0f;
        float blendFactorB_ = 1.0f;
        float blendFactorA_ = 1.0f;
        bool scissorEnabled_ = false;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorW_ = 0;
        int scissorH_ = 0;
        // REMED-GFX-064: current GraphicsDevice.Viewport, set by SetViewport() and snapshotted into
        // each QueuedDrawRef at Queue*Draw()/QueueSprite() time (PushDrawOrder). viewportSet_ stays
        // false until the game/GraphicsDevice pushes a viewport, so pre-viewport draws fall back to
        // the pass's full render-target extents (SDL's own default), byte-identical to pre-fix.
        bool viewportSet_ = false;
        int viewportX_ = 0;
        int viewportY_ = 0;
        int viewportW_ = 0;
        int viewportH_ = 0;
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;
        // REMED-GFX-051: captured by value into RenderStateSnapshot for deferred replay.
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;

        // SDLGPU-21: one entry per GraphicsDevice.SamplerStates[slot] (16, matching
        // SamplerStateCollection::MaxSamplers), set by ApplySamplerState() and read directly into
        // each DrawCommand's own textureFilter/addressU/addressV fields at Queue*Draw() time.
        std::array<SamplerSlotState, 16> samplerSlots_;

        ///@{ REMED-GFX-173: monotonic counters for CNA_SDLGPU_ENVMAP_TRACE only -- never read by
        /// any rendering decision. Per-device rather than process-global so two devices in one
        /// process each number their own draws.
        std::uint32_t envMapTraceQueueIndex_ = 0;
        std::uint32_t envMapTraceReplayIndex_ = 0;
        ///@}
    };
}
