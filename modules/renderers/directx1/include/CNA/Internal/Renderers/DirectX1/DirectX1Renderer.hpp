#pragma once

// plans/plan_dx1.md: real DirectX 1 (DirectDraw v1) graphics renderer. 2D-only --
// every 3D entry point throws (Phase O7). Unlike DIRECTX3 (which fronts the ../free-direct sibling
// reimplementation), this renderer talks to a REAL Windows ddraw.h, using only v1 COM interfaces
// (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC -- never IDirectDraw2+/Surface2+/DDSURFACEDESC2,
// plans/plan_dx1.md section 1), cross-compiled via MinGW-w64 and run under Wine/Proton, the same Route B
// delivery mechanism D3D9/D3D11/D3D12 already use.
//
// This header intentionally does NOT include <ddraw.h> (design decision 9's containment rule,
// mirroring DIRECTX3/D3D11/D3D12): it pulls in the full real <windows.h>, whose enormous macro surface
// (near/far/ERROR/…) has no business leaking into every translation unit that merely wants to hold
// a DirectX1Renderer pointer. All real <ddraw.h> usage lives in DirectX1Renderer.cpp behind the
// Impl pimpl below.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <memory>

namespace CNA::Internal::Renderers::DirectX1
{
    /**
     * DIRECTX1 graphics renderer (plans/plan_dx1.md): a CPU 2D compositor that uses a REAL
     * IDirectDraw/IDirectDrawSurface (v1 only) as its pixel storage/present mechanism. Real device/
     * window bring-up (Phase O2): DirectDrawCreate -> SetCooperativeLevel(DDSCL_NORMAL, against a
     * real Win32 HWND supplied in RendererSurfaceInfo) -> primary CreateSurface. No SetDisplayMode
     * call is ever
     * made: windowed (DDSCL_NORMAL) DirectDraw never needs one (confirmed both by reading ddraw.h
     * and empirically at the DX1-0 spike). The DX1-0 spike also found that, with no SetDisplayMode
     * call, the primary surface real Wine ddraw.dll hands back is desktop-sized (the real historical
     * DirectDraw model: the primary IS the display, not "this window") -- so this renderer never
     * composites directly onto it. Instead it owns a second, Lockable offscreen "shadow backbuffer"
     * surface, sized to the logical/virtual resolution, that Clear()/SpriteBatch draws always
     * target; Present() letterbox-scales that shadow buffer onto the primary via a single Blt(),
     * with the destination rect recomputed every frame from the window's real client area
     * (GetClientRect + ClientToScreen) -- unlike DIRECTX3's own documented stale-scale limitation
     * (plans/plan_freedirect.md DX3-16), a virtual-resolution or window-resize change is correct on the very next
     * Present(), since nothing here is cached.
     *
     * Textures and render targets (Phase O3) are real: both are private offscreen
     * DDSCAPS_OFFSCREENPLAIN surfaces (DirectX1TextureRenderer/DirectX1RenderTargetRenderer, defined entirely in
     * DirectX1Renderer.cpp -- neither is ever named outside it), and SetRenderTarget2D() redirects
     * Clear()/ReadBackbuffer() to whichever surface is currently bound. The SpriteBatch CPU
     * compositor (Phase O4, DirectX1SpriteBatchRenderer) is ported verbatim from DIRECTX3's own
     * already-verified CompositeQuad: IDirectDrawSurface::Blt/BltFast has never supported rotation
     * in any DirectX version, so an identity-transform Opaque draw is a genuine BltFast straight
     * copy; every other draw (rotation/scale/tint/flip/custom transform, or any non-Opaque blend
     * mode) is composited manually, pixel by pixel, through Lock()/Unlock() on both the source and
     * destination surfaces. Blend math (Phase O5) is real and distinct per mode -- Opaque/
     * AlphaBlend/NonPremultiplied/Additive are detected from ApplyBlendState()'s raw factors and
     * each use their own formula, matching BlendState.cpp's actual preset semantics; any other/
     * custom factor combination falls back to AlphaBlend behavior. Texture sampling supports both
     * Point/nearest and Linear/bilinear filtering, and Wrap/Mirror/Clamp addressing.
     */
    class DirectX1Renderer final : public IGraphicsRenderer
    {
    public:
        explicit DirectX1Renderer(const GraphicsRendererCreateArgs& args);
        ~DirectX1Renderer() override;

        DirectX1Renderer(const DirectX1Renderer&) = delete;
        DirectX1Renderer& operator=(const DirectX1Renderer&) = delete;

        // ---- IGraphicsRenderer: real (Phase O2) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        // A real letterbox scale+offset transform (uniform scale to fit, centered), recomputed from
        // the current physical drawable size -- shares the exact math Present() itself
        // uses (ComputeLetterbox), so these two are always consistent with what's actually on
        // screen.
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        // ---- IGraphicsRenderer: real (Phase O3) ----
        // Both CreateTexture() and CreateRenderTarget2D() own a private offscreen
        // DDSCAPS_OFFSCREENPLAIN surface (32bpp, design decision 7) sized to the request;
        // SetRenderTarget2D()/SetRenderTargets() redirect Clear()/ReadBackbuffer() (and, from
        // Phase O4, the SpriteBatch compositor) to whichever surface is currently bound, via Impl's
        // currentTargetSurface slot -- Present() always targets the real shadow backbuffer
        // regardless of binding, matching FNA's own backbuffer-vs-render-target separation.
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        // DirectDraw has no multi-render-target concept -- throws for count > 1, same conclusion
        // the other 2D backends already reached.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        // ---- IGraphicsRenderer: real (Phase O4/O5) ----
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        // Phase O5 (design decision 6): detects which of the 4 BlendState presets (or a custom
        // combination) the raw factors match; gates the SpriteBatch identity fast path (Opaque
        // only) and selects CompositeQuad's per-formula blend math.
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        // ---- 3D pipeline: NOT supported by DIRECTX1 -- DirectX 1 shipped no Direct3D at all (it did
        // not exist until DIRECTX2), so unlike DIRECTX3 (which throws by policy even though its DirectDraw
        // generation technically has an execute-buffer Direct3D sibling it deliberately never
        // uses), DIRECTX1 throws because there genuinely is no Direct3D COM interface reachable from a
        // real DirectX-1-era header pairing to even call. ----
        // @note Status: STUB. Every entry point throws std::runtime_error (CreateOcclusionQuery/
        // CreateEffectRenderer/CreateTexture3D/CreateTextureCube/CreateRenderTargetCube deliberately
        // don't override the shared nullptr-returning defaults at all -- matching DX3-66's own
        // corrected pattern). SupportsCapability() lets callers check ahead of time instead of
        // relying on the throw.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // The 3D surface is unavailable, but the CPU 2D compositor has a distinct, exact
            // DirectX1BlendMode::Additive path.
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
