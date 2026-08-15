#pragma once

// plan_freedirect.md Phase X1/X2: FreeDirect (formerly DIRECTX3) renderer skeleton + real DirectDraw (via the ../free-direct sibling)
// device/window bring-up. 2D-only, like SDL_RENDERER -- every 3D entry point throws (Phase X7).
//
// This header intentionally does NOT include <ddraw.h> (design decision 9's containment rule),
// for a stronger reason than "matches D3D11/D3D12 precedent": free-direct's <ddraw.h> pulls in
// free-api's <windows.h> compatibility shim, which globally #defines fopen -> free_api_fopen for
// every translation unit that includes it. Unlike D3D11's <d3d11.h> (no such macro), leaking that
// into this header would silently rewrite fopen() in every .cpp across the project that happens to
// include this renderer header. All real free-direct/<ddraw.h> usage lives in
// FreeDirectRenderer.cpp behind the Impl pimpl below.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::FreeDirect
{
    /**
     * @brief Resize-only failure points used by the FreeDirect transaction regression. CNAEXT.
     *
     * Hooks are copied into one renderer instance and never shared globally. Initial construction
     * is deliberately not injectable through these points; they apply only to later
     * SetVirtualResolution() replacement attempts.
     */
    enum class FreeDirectResizeFailurePointEXT : std::uint8_t
    {
        None,
        ShadowBackBufferCreation,
        ShadowBackBufferValidation,
        DisplayModeBinding,
        PrimarySurfaceCreation,
        PrimarySurfaceValidation,
        SurfaceSetCommit
    };

    /** @brief Native FreeDirect resource categories reported by FreeDirectTestHooksEXT. CNAEXT. */
    enum class FreeDirectResourceKindEXT : std::uint8_t
    {
        DirectDraw,
        PrimarySurface,
        ShadowBackBuffer
    };

    /** @brief Native acquisition/release edge reported by FreeDirectTestHooksEXT. CNAEXT. */
    enum class FreeDirectResourceEventEXT : std::uint8_t
    {
        Acquired,
        Released
    };

    /**
     * @brief Per-instance resize injection and exact native-resource lifetime observation. CNAEXT.
     *
     * `context` must outlive the renderer while `resourceEvent` is installed. `identity` is the
     * native COM interface pointer and is for identity comparison only; a Released identity must
     * never be dereferenced.
     */
    struct FreeDirectTestHooksEXT
    {
        FreeDirectResizeFailurePointEXT failAt = FreeDirectResizeFailurePointEXT::None;
        void* context = nullptr;
        void (*resourceEvent)(void* context, FreeDirectResourceKindEXT resource,
                              FreeDirectResourceEventEXT event, const void* identity) noexcept = nullptr;
    };

    /** @brief Snapshot used by the resize regression to prove native identity preservation. CNAEXT. */
    struct FreeDirectTestStateEXT
    {
        const void* directDraw = nullptr;
        const void* primarySurface = nullptr;
        const void* shadowBackBuffer = nullptr;
        const void* activeRenderTarget = nullptr;
        int logicalWidth = 0;
        int logicalHeight = 0;
        int presentationMode = 0;
    };

    /**
     * FreeDirect graphics renderer (formerly DIRECTX3; plan_freedirect.md): a CPU 2D compositor that uses free-direct's
     * IDirectDraw/IDirectDrawSurface as its pixel storage/present mechanism. Real DirectDraw
     * device/window bring-up (Phase X2): DirectDrawCreate -> SetCooperativeLevel (against CNA's
     * own already-existing SDL_Window*) -> SetDisplayMode -> primary CreateSurface. Because
     * free-direct's IDirectDrawSurface::Lock() never exposes a writable pointer for the *primary*
     * surface (verified against free-direct's own src/directdraw/DirectDraw.cpp -- GetSurfaceDesc
     * only sets lpSurface/DDSD_LPSURFACE for SurfaceType::Offscreen), this renderer never composites
     * directly onto the primary. Instead it owns a second, Lockable offscreen "shadow backbuffer"
     * surface that Clear()/SpriteBatch draws always target; Present() is a single identity Blt()
     * from the shadow buffer onto the real primary, relying on free-direct's own auto-present-on-
     * dirty-Blt behavior (Flip() is never called -- it would only disable that auto-present path
     * for no benefit, since Flip() itself does not copy from anywhere else).
     *
     * Textures and render targets (Phase X3) are real: both are private offscreen
     * DDSCAPS_OFFSCREENPLAIN surfaces (FreeDirectTextureRenderer/FreeDirectRenderTargetRenderer, defined entirely
     * in FreeDirectRenderer.cpp -- neither is ever named outside it), and SetRenderTarget2D()
     * redirects Clear()/ReadBackbuffer() to whichever surface is currently bound. The SpriteBatch
     * CPU compositor (Phase X4, FreeDirectSpriteBatchRenderer, also defined entirely in the .cpp) is real:
     * an identity-transform Opaque draw is a genuine BltFast straight copy (design decision 5's
     * cheap case); every other draw (rotation/scale/tint/flip/custom transform, or any non-Opaque
     * blend mode) is composited manually, pixel by pixel, through Lock()/Unlock() on both the
     * source and destination surfaces (FreeDirectRenderer.cpp's CompositeQuad). Blend math (Phase
     * X5, design decision 6) is real and distinct per mode -- Opaque/AlphaBlend/NonPremultiplied/
     * Additive are detected from ApplyBlendState()'s raw factors and each use their own formula,
     * matching BlendState.cpp's actual preset semantics (not a single baseline approximation);
     * any other/custom factor combination falls back to AlphaBlend behavior (DX3-44). Texture
     * sampling (design decision 7) supports both Point/nearest and Linear/bilinear filtering, and
     * Wrap/Mirror/Clamp addressing, via SetSamplerFilter()/SetSamplerAddressMode().
     */
    class FreeDirectRenderer final : public IGraphicsRenderer
    {
    public:
        explicit FreeDirectRenderer(const GraphicsRendererCreateArgs& args);
        /** @brief Test-only constructor with instance-local lifetime observation. CNAEXT. */
        FreeDirectRenderer(const GraphicsRendererCreateArgs& args, const FreeDirectTestHooksEXT& testHooks);
        ~FreeDirectRenderer() override;

        FreeDirectRenderer(const FreeDirectRenderer&) = delete;
        FreeDirectRenderer& operator=(const FreeDirectRenderer&) = delete;

        // ---- IGraphicsRenderer: real (Phase X2) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        /** @brief Replaces this instance's resize-only test hooks and rearms one failure. CNAEXT. */
        void SetTestHooksEXT(const FreeDirectTestHooksEXT& testHooks);
        /** @brief Returns native identities and resize-dependent renderer state. CNAEXT. */
        [[nodiscard]] FreeDirectTestStateEXT GetTestStateEXT() const;
        // free-direct manages its own internal SDL_Renderer privately (created lazily inside
        // PresentPrimary, against the same SDL_Window* this renderer hands to
        // SetCooperativeLevel) -- it is never exposed to CNA, so this always returns nullptr,
        // same as every other non-SDL_Renderer-based renderer.
        // DX3-68 (Phase X7): a real letterbox scale+offset transform (uniform scale to fit,
        // centered), independently recomputed from the real physical SDL_Window size -- matches
        // free-direct's own hardcoded SDL_LOGICAL_PRESENTATION_LETTERBOX behavior without needing
        // access to its internal (never-exposed) SDL_Renderer.
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        // ---- IGraphicsRenderer: real (Phase X3) ----
        // Both CreateTexture() and CreateRenderTarget2D() own a private offscreen
        // DDSCAPS_OFFSCREENPLAIN surface (32bpp, Design decision 4) sized to the request;
        // SetRenderTarget2D()/SetRenderTargets() redirect Clear()/ReadBackbuffer() (and, from
        // Phase X4, the SpriteBatch compositor) to whichever surface is currently bound, via
        // Impl's currentTargetSurface slot -- Present() always targets the real shadow backbuffer
        // regardless of binding, matching FNA's own backbuffer-vs-render-target separation.
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        // DX3-27: DirectDraw has no multi-render-target concept -- throws for count > 1, same
        // conclusion SDL_RENDERER (Task 709) and CANVAS (CANVAS-26) already reached.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        // ---- IGraphicsRenderer: real (Phase X4/X5) ----
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        // Phase X5 (design decision 6): detects which of the 4 BlendState presets (or a custom
        // combination, DX3-44) the raw factors match; gates the SpriteBatch identity fast path
        // (Opaque only) and selects CompositeQuad's per-formula blend math.
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        // ---- 3D pipeline: NOT supported by FreeDirect (DirectDraw is 2D-only). ----
        // Calls preserve their established throw/null behavior by default. WarnAndStub converts
        // them to warn-once no-ops backed by safe null-object resources.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // The renderer is 2D-only; its CPU compositor nevertheless has a distinct, exact
            // FreeDirectBlendMode::Additive path.
            return capability == CNA::GraphicsCapability::AdditiveBlending;
        }
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
