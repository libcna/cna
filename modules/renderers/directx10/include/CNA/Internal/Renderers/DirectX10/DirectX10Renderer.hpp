#pragma once

// plans/plan_d3d10.md: real Direct3D 10 graphics renderer. Unlike DIRECTX1..DIRECTX8, D3D10 removed the
// fixed-function pipeline entirely -- every draw needs a real, compiled HLSL vertex+pixel shader
// pair (vs_4_0/ps_4_0, D3D10's own shader-model ceiling), following this project's own D3DCompile
// precedent (D3D9EffectRenderer.cpp/D3D11EffectRenderer.cpp), not DIRECTX1..DIRECTX8's CPU-transform-and-submit
// model. Delivered via Wine's own builtin d3d10.dll/d3d10_1.dll (thin wrappers; DXVK 2.6.0 ships no
// d3d10.dll at all) forwarding to DXVK's real d3d10core.dll + dxgi.dll.
//
// MVP scope (plans/plan_d3d10.md design decision 3, mirroring DIRECTX1's own "baseline first, richness
// later" precedent): DrawColoredPrimitives/DrawIndexedColoredPrimitives (required by
// IGraphicsRenderer) are real shader draws (world*view*projection transform + vertex-color
// passthrough, no lighting). DrawPrimitivesEx/DrawIndexedPrimitivesEx are intentionally NOT
// overridden -- IGraphicsRenderer's own safe default falls back to the colored-primitives path, so
// draws requesting lighting/texturing via GpuDrawParams still render correctly as flat vertex
// color (XNA's own graceful degradation), just without DirectX11Renderer's own full 10-stock-
// shader-variant richness. CreateEffectRenderer/occlusion query/3D-cube-textures/instancing are
// likewise left at IGraphicsRenderer's own safe defaults.
//
// This header intentionally does NOT include <d3d10.h> (matching D3D9/D3D11/D3D12/DIRECTX1..DIRECTX8's own
// containment rule): all real <d3d10.h>/<dxgi.h>/<d3dcompiler.h> usage lives in
// DirectX10Renderer.cpp behind the Impl pimpl below.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <memory>

namespace CNA::Internal::Renderers::DirectX10
{
    /**
     * D3D10 graphics renderer (plans/plan_d3d10.md): real Direct3D 10 (ID3D10Device), delivered via
     * Wine's own builtin d3d10.dll/d3d10_1.dll forwarding to DXVK's real d3d10core.dll + dxgi.dll
     * (DXVK 2.6.0 ships no d3d10.dll of its own at all).
     *
     * Device/window bring-up: a single D3D10CreateDeviceAndSwapChain call against a real Win32
     * HWND supplied in RendererSurfaceInfo creates both the device and its own real DXGI swap
     * chain -- no logical-resolution render target or letterboxing (unlike DIRECTX8's own addition,
     * forced there by D3D8 having no scaled-blit primitive at all): the swap chain always matches
     * the real window size, matching DirectX11Renderer's own simpler, already-established
     * convention.
     *
     * Textures and render targets are real ID3D10Texture2D objects
     * (D3D10TextureRenderer/D3D10RenderTargetRenderer, defined entirely in
     * DirectX10Renderer.cpp), created via CreateTexture2D against an explicit
     * DXGI_FORMAT_R8G8B8A8_UNORM format. Readback uses CreateTexture2D(D3D10_USAGE_STAGING) +
     * CopyResource + Map(D3D10_MAP_READ) (D3D10-0f, spike-confirmed) -- D3D8's
     * CreateImageSurface+CopyRects and D3D9's GetRenderTargetData are both gone.
     *
     * The 2D SpriteBatch layer (D3D10SpriteBatchRenderer) is real GPU-rendered textured quads
     * through a real vs_4_0/ps_4_0 shader pair (position+color+texture, D3D10-0d/0e's own proven
     * shader) -- an orthographic screen-to-clip-space transform (no half-texel correction needed,
     * unlike D3D8/D3D9's own pre-D3D10 pixel-center convention: D3D10 places texel/pixel centers
     * at pixel CENTERS like every modern API, matching D3D11's own convention). Blend/depth-
     * stencil/rasterizer/sampler state are real D3D10 STATE OBJECTS
     * (ID3D10BlendState/DepthStencilState/RasterizerState/SamplerState), not per-call render-state
     * setters like DIRECTX1..DIRECTX8's D3DRENDERSTATE_ / D3DRS_ constants -- there is no equivalent of
     * SetRenderState(D3DRS_xxx, value) in this API at all.
     *
     * MRT is real (SetRenderTargets(count>1) does not throw) -- a genuine, real difference from
     * every DIRECTX1..DIRECTX8 renderer (DirectDraw/early-Direct3D has exactly one active render target).
     */
    class DirectX10Renderer final : public IGraphicsRenderer
    {
    public:
        explicit DirectX10Renderer(const GraphicsRendererCreateArgs& args);
        ~DirectX10Renderer() override;

        DirectX10Renderer(const DirectX10Renderer&) = delete;
        DirectX10Renderer& operator=(const DirectX10Renderer&) = delete;

        // ---- IGraphicsRenderer: device bring-up and 2D compositing ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        // ---- IGraphicsRenderer: textures and render targets (real ID3D10Texture2D) ----
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        // Real MRT support -- does NOT throw for count > 1 (a genuine difference from every
        // DIRECTX1..DIRECTX8 renderer), up to D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT (8).
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        // ---- IGraphicsRenderer: 2D compositor + state objects ----
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
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

        // ---- 3D pipeline: real vs_4_0/ps_4_0 shader (MVP scope, see file header) ----
        [[nodiscard]] bool SupportsDepthStencil() const override { return true; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // ThreeD/DepthStencilBuffer/WireFrame/AnisotropicFiltering: real GPU via DXVK.
            // MultipleRenderTargets: real (unlike every DIRECTX1..DIRECTX8 renderer) -- D3D10 has genuine
            // OMSetRenderTargets(count>1,...) support. OcclusionQuery/CustomEffects: out of this
            // v1's scope (plans/plan_d3d10.md design decision 3), not a hardware limitation.
            using CNA::GraphicsCapability;
            switch (capability)
            {
                case GraphicsCapability::ThreeD:
                case GraphicsCapability::DepthStencilBuffer:
                case GraphicsCapability::WireFrame:
                case GraphicsCapability::AnisotropicFiltering:
                case GraphicsCapability::MultipleRenderTargets:
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
        // Real ID3D10Buffer-backed storage (D3D10VertexBufferRenderer/D3D10IndexBufferRenderer,
        // defined entirely in DirectX10Renderer.cpp).
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;
        // Real vs_4_0/ps_4_0 shader draw: world*view*projection transform via a constant buffer,
        // vertex-color passthrough (matches BasicEffect(VertexColorEnabled=true), no lighting --
        // MVP scope, file header). Near/far-plane clipping is real GPU hardware clipping (no CPU
        // Sutherland-Hodgman needed, unlike DIRECTX1..DIRECTX8).
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        // DrawPrimitivesEx/DrawIndexedPrimitivesEx intentionally NOT overridden -- see file header.

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
