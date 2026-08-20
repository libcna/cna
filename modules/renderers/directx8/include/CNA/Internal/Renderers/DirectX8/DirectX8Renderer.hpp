#pragma once

// plans/plan_dx8.md: real DirectX 8 graphics renderer -- architecturally very different from DIRECTX1..DIRECTX7.
// "DirectDraw+Direct3D merged" (no DirectDraw at all): a single IDirect3D8::CreateDevice call
// creates both the device and its own swap chain. Delivered via DXVK (D8VK, merged into DXVK
// 2.0+), not Wine's own d3d8.dll -- mingw-w64's x86_64 target ships no real d3d8 import library at
// all, so this renderer links DXVK's own d3d8.dll.a directly (design decision 2). Scope decision
// (made with the project owner before any code was written): fixed-function 3D only, matching
// DIRECTX1..DIRECTX7's own CPU-transform-and-submit shape -- real XNA effects need ps_2_0+ regardless of
// Shader Model 1.x support, so a real SM1.x pipeline would not make CreateEffectRenderer usable for
// actual XNA content. The 2D SpriteBatch layer is REDESIGNED (not ported) as real GPU-rendered
// textured quads with real alpha blending (design decision 11) -- DirectDraw's Blt/BltFast, which
// DIRECTX1..DIRECTX7 all relied on, does not exist at this era at all.
//
// This header intentionally does NOT include <d3d8.h> (matching D3D9/D3D11/D3D12/DIRECTX1..DIRECTX7's own
// containment rule): it pulls in the full real <windows.h>, whose enormous macro surface has no
// business leaking into every translation unit that merely wants to hold a DirectX8Renderer
// pointer. All real <d3d8.h> usage lives in DirectX8Renderer.cpp behind the Impl pimpl below.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <memory>

namespace CNA::Internal::Renderers::DirectX8
{
    /**
     * DIRECTX8 graphics renderer (plans/plan_dx8.md): real DirectX 8 (IDirect3D8/IDirect3DDevice8), delivered
     * via DXVK (Direct3DCreate8 resolved from DXVK's own d3d8.dll.a, design decision 2 -- mingw-w64
     * ships no real d3d8 import library for x86_64). Device/window bring-up (design decision 3,
     * modeled on DirectX9Renderer's own shape rather than DIRECTX1..DIRECTX7's DirectDraw-based one): a
     * single Direct3DCreate8 + IDirect3D8::CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
     * D3DCREATE_SOFTWARE_VERTEXPROCESSING, &presentParams, &device) call creates both the device
     * and its own swap chain against the real Win32 HWND supplied in RendererSurfaceInfo -- no separate DirectDraw
     * object, no manual "shadow backbuffer + Blt to primary" trick this whole family needed since
     * DX2-0. D3DPRESENT_PARAMETERS construction has one real, spike-confirmed (DX8-0) rule:
     * FullScreen_PresentationInterval must be D3DPRESENT_INTERVAL_DEFAULT in windowed mode, or
     * CreateDevice fails with D3DERR_INVALIDCALL.
     *
     * Textures and render targets are real Direct3D8 resources (DirectX8TextureRenderer/
     * DirectX8RenderTargetRenderer, defined entirely in DirectX8Renderer.cpp -- neither is ever named
     * outside it), created via IDirect3DDevice8::CreateTexture/CreateRenderTarget/
     * CreateDepthStencilSurface against an explicit D3DFMT_A8R8G8B8 format -- no DirectDraw-style
     * "detect the negotiated native format" dance is needed at all, since D3D8 lets this renderer
     * request its own known format directly. SetRenderTarget2D() redirects Clear()/ReadBackbuffer()
     * to whichever surface is currently bound. The SpriteBatch compositor (DirectX8SpriteBatchRenderer,
     * design decision 11) is REDESIGNED, not ported: real GPU-rendered textured quads through the
     * SAME fixed-function pipeline 3D geometry uses, with real D3DRS_ALPHABLENDENABLE/SRCBLEND/
     * DESTBLEND state (whatever ApplyBlendState() last set) -- rotation, scale, and all 4
     * BlendState presets are real GPU features here, not a CPU-approximated formula per mode the
     * way DIRECTX1..DIRECTX7's DirectDraw-Blt-based compositor needed (DirectDraw does not exist at this
     * DirectX era at all).
     *
     * The 3D layer is real from day one, fixed-function only (no Shader Model 1.x, scope decision
     * above): CPU transform + near-plane clip, packed into a hand-defined DirectX8TLVertex struct (the
     * canned D3DTLVERTEX macro no longer exists in the real headers, but the FVF values --
     * D3DFVF_XYZRHW/DIFFUSE/SPECULAR/TEX1 -- still do, reproducing the identical byte layout),
     * submitted via SetVertexShader(rawFvfValue) + DrawIndexedPrimitiveUP (a real, confusing
     * D3D8-only idiom -- passing a raw FVF DWORD directly as if it were a vertex-shader handle,
     * spike-confirmed real). Real depth-test occlusion, real texture sampling via SetTexture +
     * SetTextureStageState(D3DTSS_COLOROP, D3DTOP_MODULATE) (the modern mechanism DIRECTX7's own design
     * decision 6 already established, used here from the start -- no repeat of DX7-0's own
     * D3DRENDERSTATE_TEXTUREMAPBLEND-rejection surprise), full per-draw state mapping, and CPU-side
     * BasicEffect lighting (ambient + directional Lambertian/Blinn-Phong specular) are all real,
     * ported conceptually from DIRECTX7's own already-proven CPU pipeline. Stencil
     * (D3DRS_STENCILENABLE/FUNC/FAIL/ZFAIL/PASS/REF/MASK/WRITEMASK against a D3DFMT_D24S8
     * auto-depth-stencil surface) is unchanged in shape from DIRECTX6/DIRECTX7, only the render-state naming
     * changed (D3DRENDERSTATE_* -> D3DRS_*, identical underlying enum values). Readback uses
     * CreateImageSurface+CopyRects (D3D8 predates D3D9's GetRenderTargetData).
     */
    class DirectX8Renderer final : public IGraphicsRenderer
    {
    public:
        explicit DirectX8Renderer(const GraphicsRendererCreateArgs& args);
        ~DirectX8Renderer() override;

        DirectX8Renderer(const DirectX8Renderer&) = delete;
        DirectX8Renderer& operator=(const DirectX8Renderer&) = delete;

        // ---- IGraphicsRenderer: device bring-up and 2D compositing ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        // A real letterbox scale+offset transform (uniform scale to fit, centered), recomputed
        // from the current physical drawable size -- shares the exact math Present()
        // itself uses, so these two are always consistent with what's actually on screen.
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        // ---- IGraphicsRenderer: textures and render targets (real Direct3D8 resources) ----
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        // No multi-render-target concept at this DirectX era -- throws for count > 1, same
        // conclusion every DirectDraw-based renderer in this family already reached.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        // ---- IGraphicsRenderer: 2D compositor + state mapping ----
        // Design decision 11: real GPU-rendered textured quads (DirectX8SpriteBatchRenderer), not a
        // CPU compositor -- DirectDraw does not exist at this DirectX era at all.
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        // Real D3DRS_ALPHABLENDENABLE/SRCBLEND/DESTBLEND for both 3D draws and 2D SpriteBatch
        // quads (design decision 11 -- both paths share the same device blend state). D3D8 has no
        // separate alpha blend-factor/op pair -- alphaSrcBlend/alphaDstBlend/colorBlendFunc/
        // alphaBlendFunc are accepted and ignored, matching every prior renderer in this family.
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        // plans/plan_dx8.md design decision 9: ApplyDepthStencilState honors depth AND the front-face
        // stencil parameters (enable/func/fail/zfail/pass/mask/writemask/ref) -- unchanged in
        // shape from DIRECTX6/DIRECTX7, only the render-state naming changed (D3DRS_* not
        // D3DRENDERSTATE_*). twoSidedStencilMode/ccwStencil* remain accepted-and-ignored:
        // two-sided stencil is a D3D9-era addition that doesn't exist at this DirectX era at all.
        // ApplyRasterizerState honors cullMode/fillMode; scissorTestEnable/depthBias/
        // slopeScaleDepthBias are accepted and ignored (no scissor test or depth-bias render
        // state exists at this era). ApplySamplerState only acts on slot 0 (this renderer's own
        // fixed-function scope has exactly one texture stage) and honors filter/addressU/
        // maxAnisotropy; addressV is accepted and ignored (D3DRS_TEXTUREADDRESS-equivalent is a
        // single combined U+V mode at this era, no separate per-axis render states exist).
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

        // ---- 3D pipeline: real, fixed-function only (scope decision, TL;DR), built on
        // IDirect3DDevice8::DrawIndexedPrimitiveUP via SetVertexShader(rawFvfValue) -- no vertex
        // buffer object, no Shader Model 1.x. What remains out of scope is documented per-method
        // below and in plans/plan_dx8.md's own Boundaries section. ----
        [[nodiscard]] bool SupportsDepthStencil() const override { return true; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // ThreeD/DepthStencilBuffer/WireFrame real (matching DIRECTX7's own post-Phase-O9 finding
            // that D3DFILL_WIREFRAME genuinely renders edge-only output on a software rasterizer
            // -- DXVK's real GPU rasterizer honors it too). MultiSampleAntiAliasing/
            // MultipleRenderTargets/OcclusionQuery/CustomEffects genuinely unavailable at this
            // DirectX era (plans/plan_dx8.md's Boundaries section). AnisotropicFiltering: unlike DIRECTX7's
            // software-rasterizer finding, this renderer runs on a real GPU via DXVK, so
            // anisotropic filtering IS a real, distinct capability here -- reports true.
            using CNA::GraphicsCapability;
            switch (capability)
            {
                case GraphicsCapability::ThreeD:
                case GraphicsCapability::DepthStencilBuffer:
                case GraphicsCapability::WireFrame:
                case GraphicsCapability::AnisotropicFiltering:
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
        // Design decision 10: plain CPU-side storage (DirectX8VertexBufferRenderer/
        // DirectX8IndexBufferRenderer, defined entirely in DirectX8Renderer.cpp), matching this
        // family's own Phase O5 pattern (and the Software renderer's identical approach) -- the CPU
        // transform pipeline reads directly from these buffers each draw, so there is no GPU-side
        // object to upload to. CreateIndexBuffer32 is explicitly overridden (real 32-bit storage)
        // rather than relying on the shared default's delegate-to-16-bit fallback.
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

        // Effect-aware draws: stride-dispatched vertex layouts, real texture0 modulation via
        // SetTexture+SetTextureStageState (design decision 7). Overridden explicitly rather than
        // relying on the shared IGraphicsRenderer default (which falls back to
        // DrawColoredPrimitives/DrawIndexedColoredPrimitives and would silently ignore
        // params.texture0). Fog/multitexture/envMap/skinning are read from `params` but not
        // evaluated -- accepted and ignored, matching this family's own already-documented
        // fixed-function scope boundary -- the vertex's own diffuse color is used as-is.
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
