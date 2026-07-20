#pragma once

// plan_dx2.md: real DirectX 2 graphics backend. Its 2D layer (this phase, O1/O2) is a verbatim
// port of DX1 (plan_dx1.md) -- same real Windows ddraw.h, same v1-only COM interfaces
// (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC -- never IDirectDraw2+/Surface2+/DDSURFACEDESC2),
// cross-compiled via MinGW-w64 and run under Wine/Proton, the same Route B delivery mechanism
// D3D9/D3D11/D3D12/DX1 already use. Unlike DX1 (which has no Direct3D to call at all -- Direct3D
// did not exist until DX2), this backend's 3D layer is real, built on IDirect3D2/IDirect3DDevice2
// DrawPrimitive (not execute buffers, see plan_dx2.md's status note) -- landing in a later phase
// (O3/O4). Every 3D entry point currently still throws/degrades exactly as DX1's do (Phase O1/O2
// scope); this is a temporary, pre-O3 state, not a permanent boundary the way it is for DX1.
//
// This header intentionally does NOT include <ddraw.h> (design decision 9's containment rule,
// mirroring DX3/D3D11/D3D12): it pulls in the full real <windows.h>, whose enormous macro surface
// (near/far/ERROR/…) has no business leaking into every translation unit that merely wants to hold
// a Dx2GraphicsBackend pointer. All real <ddraw.h> usage lives in Dx2GraphicsBackend.cpp behind the
// Impl pimpl below.

#include "../Common/IGraphicsBackend.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace CNA::Internal::Backends::Dx2
{
    /**
     * DX2 graphics backend (plan_dx2.md): its 2D layer is a verbatim port of DX1's own
     * (plan_dx1.md) CPU 2D compositor that uses a REAL IDirectDraw/IDirectDrawSurface (v1 only) as
     * its pixel storage/present mechanism. Real device/window bring-up (Phase O2, ported from
     * DX1's Phase O2): DirectDrawCreate -> SetCooperativeLevel(DDSCL_NORMAL, against a real Win32
     * HWND obtained from CNA's own already-existing SDL_Window via
     * SDL_PROP_WINDOW_WIN32_HWND_POINTER) -> primary CreateSurface. No SetDisplayMode call is ever
     * made: windowed (DDSCL_NORMAL) DirectDraw never needs one (confirmed both by reading ddraw.h
     * and empirically at DX1's own DX1-0 spike, which this backend inherits unchanged). DX1-0 also
     * found that, with no SetDisplayMode call, the primary surface real Wine ddraw.dll hands back
     * is desktop-sized (the real historical DirectDraw model: the primary IS the display, not
     * "this window") -- so this backend never composites directly onto it. Instead it owns a
     * second, Lockable offscreen "shadow backbuffer" surface (plan_dx2.md design decision 4: given
     * DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE, not DX1's plain DDSCAPS_OFFSCREENPLAIN, so a later
     * phase can attach a Z-buffer and create a Direct3D device against this same surface), sized to
     * the logical/virtual resolution, that Clear()/SpriteBatch draws always target; Present()
     * letterbox-scales that shadow buffer onto the primary via a single Blt(), with the destination
     * rect recomputed every frame from the window's real client area (GetClientRect +
     * ClientToScreen) -- unlike DX3's own documented stale-scale limitation (plan_dx3.md DX3-16), a
     * virtual-resolution or window-resize change is correct on the very next Present(), since
     * nothing here is cached.
     *
     * Textures and render targets (Phase O3, ported from DX1's Phase O3) are real: both are
     * private offscreen DDSCAPS_OFFSCREENPLAIN surfaces (Dx2TextureBackend/Dx2RenderTargetBackend,
     * defined entirely in Dx2GraphicsBackend.cpp -- neither is ever named outside it), and
     * SetRenderTarget2D() redirects Clear()/ReadBackbuffer() to whichever surface is currently
     * bound. The SpriteBatch CPU compositor (Phase O4, Dx2SpriteBatchBackend, ported from DX1's
     * Phase O4, itself ported verbatim from DX3's own already-verified CompositeQuad):
     * IDirectDrawSurface::Blt/BltFast has never supported rotation in any DirectX version, so an
     * identity-transform Opaque draw is a genuine BltFast straight copy; every other draw
     * (rotation/scale/tint/flip/custom transform, or any non-Opaque blend mode) is composited
     * manually, pixel by pixel, through Lock()/Unlock() on both the source and destination
     * surfaces. Blend math (Phase O5, ported from DX1's Phase O5) is real and distinct per mode --
     * Opaque/AlphaBlend/NonPremultiplied/Additive are detected from ApplyBlendState()'s raw factors
     * and each use their own formula, matching BlendState.cpp's actual preset semantics; any
     * other/custom factor combination falls back to AlphaBlend behavior. Texture sampling supports
     * both Point/nearest and Linear/bilinear filtering, and Wrap/Mirror/Clamp addressing.
     *
     * The 3D layer (Direct3D v2, IDirect3D2/IDirect3DDevice2::DrawPrimitive) is NOT part of this
     * phase -- see the note above the 3D method group below. It lands in Phase O3/O4.
     */
    class Dx2GraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit Dx2GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~Dx2GraphicsBackend() override;

        Dx2GraphicsBackend(const Dx2GraphicsBackend&) = delete;
        Dx2GraphicsBackend& operator=(const Dx2GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: real (Phase O2) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        SDL_Window* GetWindowInternal() const override;
        // No real IDirectDraw-owned SDL_Renderer exists here at all (this backend never creates
        // one, unlike SDL_RENDERER) -- always nullptr, same as every other non-SDL_Renderer-based
        // backend.
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }
        // A real letterbox scale+offset transform (uniform scale to fit, centered), recomputed from
        // the real physical SDL_Window size on every call -- shares the exact math Present() itself
        // uses (ComputeLetterbox), so these two are always consistent with what's actually on
        // screen.
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        // ---- IGraphicsBackend: real (Phase O3) ----
        // Both CreateTexture() and CreateRenderTarget2D() own a private offscreen
        // DDSCAPS_OFFSCREENPLAIN surface (32bpp, design decision 7) sized to the request;
        // SetRenderTarget2D()/SetRenderTargets() redirect Clear()/ReadBackbuffer() (and, from
        // Phase O4, the SpriteBatch compositor) to whichever surface is currently bound, via Impl's
        // currentTargetSurface slot -- Present() always targets the real shadow backbuffer
        // regardless of binding, matching FNA's own backbuffer-vs-render-target separation.
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        // DirectDraw has no multi-render-target concept -- throws for count > 1, same conclusion
        // SDL_RENDERER/CANVAS/DX3 already reached.
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;

        // ---- IGraphicsBackend: real (Phase O4/O5) ----
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        // Phase O5 (design decision 6): detects which of the 4 BlendState presets (or a custom
        // combination) the raw factors match; gates the SpriteBatch identity fast path (Opaque
        // only) and selects CompositeQuad's per-formula blend math.
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;

        // ---- 3D pipeline: device/viewport/Z-buffer bring-up (Phase O3) is real; the draw path
        // itself (Phase O4/O5 -- VertexBuffer/IndexBuffer, DrawColoredPrimitives/DrawPrimitivesEx)
        // is NOT yet implemented. Unlike DX1 (which throws PERMANENTLY -- DirectX 1 shipped no
        // Direct3D at all, so there is genuinely no COM interface reachable from a real
        // DirectX-1-era header pairing to even call), DX2 (1996) is the first DirectX release with
        // real Direct3D, and this backend's own plan commits to real 3D via
        // IDirect3D2/IDirect3DDevice2::DrawPrimitive (not execute buffers -- proven non-functional
        // in this environment, see plan_dx2.md's status note). Every 3D entry point below still
        // temporarily throws/degrades exactly as DX1's do until Phase O4/O5 lands (Clear*
        // depth/color entry points are the exception -- real as of Phase O3, see below) -- not a
        // permanent boundary the way it is for DX1. ----
        // @note Status: STUB for the draw path (Phase O3 only). Every draw/buffer entry point still
        // throws std::runtime_error (CreateOcclusionQuery/CreateEffectBackend/CreateTexture3D/
        // CreateTextureCube/CreateRenderTargetCube deliberately don't override the shared
        // nullptr-returning defaults at all -- matching DX1/DX3-66's own corrected pattern).
        // SupportsCapability() lets callers check ahead of time instead of relying on the throw.
        [[nodiscard]] bool SupportsDepthStencil() const override { return true; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability /*capability*/) const override
        {
            // Phase O3 status quo: the 3D DEVICE (viewport/Z-buffer/Clear*) is real, but the 3D
            // PIPELINE as CNA::GraphicsCapability::ThreeD defines it (vertex/index buffers, 3D draw
            // calls, depth/stencil clears AND state -- IGraphicsCapability.hpp's own bundled
            // definition) is not usable yet: CreateVertexBuffer/DrawColoredPrimitives etc. still
            // throw until Phase O4/O5 lands. Every CNA::GraphicsCapability value therefore still
            // reports false here (SupportsDepthStencil() above is a narrower, differently-scoped
            // query -- specifically about whether Clear(ClearOptions) can route to this backend's
            // now-real ClearColorAndDepth/ClearDepth/etc, which it now genuinely can).
            return false;
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
        // Phase O5 (design decision 8): plain CPU-side storage (Dx2VertexBufferBackend/
        // Dx2IndexBufferBackend, defined entirely in Dx2GraphicsBackend.cpp), matching the
        // Software backend's own identical approach -- Phase O4's CPU transform pipeline reads
        // directly from these buffers each draw, so there is no GPU-side object to upload to.
        // CreateIndexBuffer32 is explicitly overridden (real 32-bit storage) rather than relying
        // on the shared default's delegate-to-16-bit fallback, which would silently truncate a
        // 32-bit index request.
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        // Effect-aware draws (design decisions 6/7): stride-dispatched vertex layouts, real
        // texture0 modulation via D3DRENDERSTATE_TEXTUREHANDLE. Overridden explicitly rather than
        // relying on the shared IGraphicsBackend default (which falls back to
        // DrawColoredPrimitives/DrawIndexedColoredPrimitives and would silently ignore
        // params.texture0). Lighting/fog/multitexture/envMap/skinning are read from `params` but
        // not evaluated -- accepted and ignored, matching the Software backend's own identical,
        // already-documented v1 scope boundary (design decision 7) -- the vertex's own diffuse
        // color is used as-is.
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount,
                             const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
