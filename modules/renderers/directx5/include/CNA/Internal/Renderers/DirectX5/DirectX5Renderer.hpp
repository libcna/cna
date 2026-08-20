#pragma once

// plans/plan_dx5.md: real DirectX 5 graphics renderer -- DirectDraw v4 (IDirectDraw4/
// IDirectDrawSurface4/DDSURFACEDESC2) + Direct3D v3 (IDirect3D3/IDirect3DDevice3/
// IDirect3DViewport3), the first DirectX release where execute buffers are gone entirely
// (DrawPrimitive/DrawIndexedPrimitive only, selected via a DWORD FVF bitmask -- D3DFVF_TLVERTEX --
// instead of the old D3DVERTEXTYPE enum). Its 2D layer is a mechanical port of DIRECTX3's own
// (plans/plan_dx3.md, itself a port of DIRECTX2's, plans/plan_dx2.md), upgraded further so EVERY surface is v4,
// not just the top DirectDraw object; its 3D layer is a port of DIRECTX3's own already-proven one
// (including Phase O9's CPU lighting), resubmitted through the new FVF parameter. Cross-compiled
// via MinGW-w64 and run under Wine/Proton, the same Route B delivery mechanism
// D3D9/D3D11/D3D12/DIRECTX1/DIRECTX2/DIRECTX3 already use.
//
// This header intentionally does NOT include <ddraw.h> (design decision 9's containment rule,
// mirroring DIRECTX3/D3D11/D3D12): it pulls in the full real <windows.h>, whose enormous macro surface
// (near/far/ERROR/…) has no business leaking into every translation unit that merely wants to hold
// a DirectX5Renderer pointer. All real <ddraw.h> usage lives in DirectX5Renderer.cpp behind the
// Impl pimpl below.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <memory>

namespace CNA::Internal::Renderers::DirectX5
{
    /**
     * DIRECTX5 graphics renderer (plans/plan_dx5.md): a mechanical port of DirectX3Renderer (plans/plan_dx3.md,
     * itself a port of DirectX2Renderer, plans/plan_dx2.md), upgraded to DirectDraw v4
     * (IDirectDraw4/IDirectDrawSurface4/DDSURFACEDESC2/DDSCAPS2 throughout -- every surface, not
     * just the top DirectDraw object) and Direct3D v3 (IDirect3D3/IDirect3DDevice3/
     * IDirect3DViewport3, DrawPrimitive/DrawIndexedPrimitive selected via the D3DFVF_TLVERTEX FVF
     * bitmask instead of the old D3DVERTEXTYPE enum). Device/window bring-up: DirectDrawCreate ->
     * QueryInterface(IID_IDirectDraw4) -> SetCooperativeLevel(DDSCL_NORMAL, against a real Win32
     * HWND supplied in RendererSurfaceInfo) -> primary CreateSurface. No SetDisplayMode call is ever
     * made: windowed (DDSCL_NORMAL) DirectDraw never needs one (confirmed both by reading ddraw.h
     * and empirically at DX1-0/DX2-0/DX30-0/DX5-0, all of which this renderer's history inherits).
     * The primary surface is desktop-sized (the real historical DirectDraw model: the primary IS
     * the display, not "this window") -- so this renderer never composites directly onto it.
     * Instead it owns a second, Lockable offscreen "shadow backbuffer" surface (DDSCAPS_OFFSCREENPLAIN
     * | DDSCAPS_3DDEVICE, so a Z-buffer can be attached and a Direct3D device created against this
     * same surface), sized to the logical/virtual resolution, that Clear()/SpriteBatch draws
     * always target; Present() letterbox-scales that shadow buffer onto the primary via a single
     * Blt(), with the destination rect recomputed every frame from the window's real client area.
     *
     * Textures and render targets are real, private offscreen DDSCAPS_OFFSCREENPLAIN v4 surfaces
     * (DirectX5TextureRenderer/DirectX5RenderTargetRenderer, defined entirely in DirectX5Renderer.cpp --
     * neither is ever named outside it), and SetRenderTarget2D() redirects Clear()/
     * ReadBackbuffer() to whichever surface is currently bound. The SpriteBatch CPU compositor
     * (DirectX5SpriteBatchRenderer): IDirectDrawSurface4::Blt/BltFast has never supported rotation in any
     * DirectX version, so an identity-transform Opaque draw is a genuine BltFast straight copy;
     * every other draw is composited manually, pixel by pixel, through Lock()/Unlock() on both the
     * source and destination surfaces. Blend math is real and distinct per mode --
     * Opaque/AlphaBlend/NonPremultiplied/Additive are detected from ApplyBlendState()'s raw
     * factors and each use their own formula. Texture sampling supports both Point/nearest and
     * Linear/bilinear filtering, and Wrap/Mirror/Clamp addressing.
     *
     * The 3D layer (Direct3D v3, IDirect3D3/IDirect3DDevice3::DrawPrimitive) is real from day one
     * -- CPU transform + near-plane clip, D3DTLVERTEX packing (submitted via the new FVF
     * parameter), real depth-test occlusion, real texture sampling, full per-draw state mapping,
     * and Phase O9's CPU-side BasicEffect lighting (ambient + directional Lambertian/Blinn-Phong
     * specular via real D3DRENDERSTATE_SPECULARENABLE) -- all ported from DIRECTX3's own already-
     * proven 3D layer. IDirect3DViewport3::Clear2 provides a real, direct depth/color/stencil
     * clear with explicit values, replacing DIRECTX2/DIRECTX3's manual Z-buffer Lock() workaround.
     */
    class DirectX5Renderer final : public IGraphicsRenderer
    {
    public:
        explicit DirectX5Renderer(const GraphicsRendererCreateArgs& args);
        ~DirectX5Renderer() override;

        DirectX5Renderer(const DirectX5Renderer&) = delete;
        DirectX5Renderer& operator=(const DirectX5Renderer&) = delete;

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
        // only) and selects CompositeQuad's per-formula blend math. Phase O6 additionally applies
        // the real D3DRENDERSTATE_SRCBLEND/DESTBLEND/ALPHABLENDENABLE render states for 3D draws
        // (D3D v1/v2 has no separate alpha blend-factor/op pair -- alphaSrcBlend/alphaDstBlend/
        // colorBlendFunc/alphaBlendFunc are accepted and ignored, decision 7's pattern).
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        // Phase O6 (design decision 10): real per-draw 3D state application, replacing the shared
        // no-op defaults. ApplyDepthStencilState honors depth (enable/write/func) only -- stencil
        // parameters are accepted and ignored (no real stencil buffer exists at this DirectX era,
        // decision 7). ApplyRasterizerState honors cullMode/fillMode; scissorTestEnable/depthBias/
        // slopeScaleDepthBias are accepted and ignored (no scissor test or depth-bias render state
        // exists in d3dtypes.h at this era -- confirmed by inspection). ApplySamplerState only
        // acts on slot 0 (D3D v1/v2 has exactly one texture stage) and honors filter/addressU/
        // maxAnisotropy; addressV is accepted and ignored (D3DRENDERSTATE_TEXTUREADDRESS is a
        // single combined U+V mode, no separate per-axis render states exist).
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        // ---- 3D pipeline: real, built on IDirect3D3/IDirect3DDevice3::DrawPrimitive (not execute
        // buffers -- gone entirely as of this device revision; see plans/plan_dx5.md section 1).
        // Device/viewport/Z-buffer bring-up (Phase O3), VertexBuffer/IndexBuffer storage (Phase
        // O5), the CPU transform/clip -> D3DTLVERTEX draw path (Phase O4), and per-draw state
        // application (Phase O6) are all real and pixel-verified. Unlike DIRECTX1 (which throws
        // PERMANENTLY -- DirectX 1 shipped no Direct3D at all, so there is genuinely no COM
        // interface reachable from a real DirectX-1-era header pairing to even call), this is a
        // working 3D pipeline, not a stub. What remains out of scope is documented per-method
        // below and in plans/plan_dx2.md's own Boundaries section (lighting/fog/multitexture/envMap/
        // skinning accepted-and-ignored; stencil/MRT/instancing/occlusion-query/volume-and-cube
        // textures/custom-effects either accepted-and-ignored or genuinely unavailable at this
        // DirectX era, not "not yet implemented"). ----
        [[nodiscard]] bool SupportsDepthStencil() const override { return true; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // Phase O6 completes the bundle CNA::GraphicsCapability::ThreeD's own doc comment
            // defines it as (vertex/index buffers, 3D draw calls, depth/stencil clears AND state)
            // -- all real as of this phase, so ThreeD now reports true. DepthStencilBuffer also
            // reports true (a real, if depth-only, buffer exists -- SupportsDepthStencil() already
            // says so). MultiSampleAntiAliasing/MultipleRenderTargets/OcclusionQuery/CustomEffects
            // report false -- genuinely unavailable at this DirectX era (plans/plan_dx2.md's Boundaries
            // section).
            //
            // WireFrame (Phase O9, plans/plan_dx2.md design decision 13): reports true. A follow-up
            // spike (dx2_spike10_specular_wireframe_aniso.cpp, Test D) empirically confirmed
            // D3DRENDERSTATE_FILLMODE=D3DFILL_WIREFRAME genuinely renders edge-only output on this
            // environment's software RGB device (a point inside a filled triangle reads back the
            // cleared background color in WIREFRAME mode, the triangle's own color in SOLID mode)
            // -- real, verified distinctness, not assumed from the render state merely existing.
            //
            // AnisotropicFiltering still reports false, but now for an EMPIRICALLY CONFIRMED
            // reason rather than "never verified": the same spike's Test E rendered a heavily-
            // minified checkerboard texture under D3DTFN_POINT/D3DTFN_LINEAR/D3DTFN_ANISOTROPIC
            // (at two different anisotropy levels) and got byte-identical readback at every
            // sampled point across all four configurations -- this software RGB device does not
            // implement anisotropic (or even point-vs-linear) minification filtering distinctly
            // at all. D3DRENDERSTATE_ANISOTROPY is still set by ApplySamplerState (it's a real,
            // accepted render state), it simply has no observable effect here.
            using CNA::GraphicsCapability;
            switch (capability)
            {
                case GraphicsCapability::ThreeD:
                case GraphicsCapability::DepthStencilBuffer:
                case GraphicsCapability::WireFrame:
                case GraphicsCapability::AdditiveBlending:
                    return true;
                default:
                    return false;
            }
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
        // Phase O5 (design decision 8): plain CPU-side storage (DirectX5VertexBufferRenderer/
        // DirectX5IndexBufferRenderer, defined entirely in DirectX5Renderer.cpp), matching the
        // Software renderer's own identical approach -- Phase O4's CPU transform pipeline reads
        // directly from these buffers each draw, so there is no GPU-side object to upload to.
        // CreateIndexBuffer32 is explicitly overridden (real 32-bit storage) rather than relying
        // on the shared default's delegate-to-16-bit fallback, which would silently truncate a
        // 32-bit index request.
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        // Effect-aware draws (design decisions 6/7): stride-dispatched vertex layouts, real
        // texture0 modulation via D3DRENDERSTATE_TEXTUREHANDLE. Overridden explicitly rather than
        // relying on the shared IGraphicsRenderer default (which falls back to
        // DrawColoredPrimitives/DrawIndexedColoredPrimitives and would silently ignore
        // params.texture0). Lighting/fog/multitexture/envMap/skinning are read from `params` but
        // not evaluated -- accepted and ignored, matching the Software renderer's own identical,
        // already-documented v1 scope boundary (design decision 7) -- the vertex's own diffuse
        // color is used as-is.
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount,
                             const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
