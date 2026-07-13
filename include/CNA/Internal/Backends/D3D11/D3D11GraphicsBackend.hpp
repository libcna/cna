#pragma once

// plan_dx.md Phase DX2/DX4: D3D11 backend skeleton + device/swap-chain/back-buffer.
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_BACKEND=D3D11).

#include "../Common/IGraphicsBackend.hpp"
#include "D3D11InputLayoutCache.hpp"
#include "D3D11SamplerCache.hpp"
#include "D3D11StateObjectCache.hpp"

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D11
{
    using Microsoft::WRL::ComPtr;

    class D3D11RenderTargetBackend;
    class D3D11RenderTargetCubeBackend;

    /**
     * D3D11 graphics backend (plan_dx.md). Implements IGraphicsBackend on top of Direct3D 11 via
     * DXGI, with real device/swap-chain/back-buffer/clear/present/readback (Phase DX4) and real
     * vertex/index buffers + input layout caching (Phase DX5). Everything Phase DX6 onward will
     * add (textures, draw calls, SpriteBatch) is still an honest "not yet implemented" stub --
     * mirrors plan_software.md/plan_headless.md's own "CnaTests must link cleanly even before most
     * methods are real" bar.
     *
     * Resource lifetime is split into three independent groups (plan_dx.md design decision 11):
     *   - Device lifetime (device_/context_/factory_/allowTearingSupported_/featureLevel_):
     *     created once in CreateDeviceResources(), only torn down on device-removed recovery.
     *   - Swap-chain lifetime (swapChain_): created once in CreateSwapChainResources(); a plain
     *     resize reuses the same object via ResizeBuffers(), never recreates it.
     *   - Window-size lifetime (backBufferRTV_/depthStencilView_/...): recreated on every resize
     *     AND on device-removed recovery, via CreateWindowSizeDependentViews().
     */
    class D3D11GraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit D3D11GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~D3D11GraphicsBackend() override;

        D3D11GraphicsBackend(const D3D11GraphicsBackend&) = delete;
        D3D11GraphicsBackend& operator=(const D3D11GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: real (Phase DX4) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        /// Exposes the negotiated feature level for tests/diagnostics (NOXNA).
        [[nodiscard]] D3D_FEATURE_LEVEL GetFeatureLevelEXT() const { return featureLevel_; }
        /// Exposes whether the debug layer actually ended up enabled (NOXNA, DX-21 diagnostics).
        [[nodiscard]] bool IsDebugLayerEnabledEXT() const { return debugLayerEnabled_; }
        /// Exposes whether the swap chain was created tearing-capable (NOXNA, DX-23 diagnostics).
        [[nodiscard]] bool IsTearingCapableEXT() const { return allowTearingSupported_ && allowTearingRequested_; }
        /// Exposes the raw device pointer for tests/diagnostics and for D3DCommon helpers (e.g.
        /// D3DShaderCache, DX-15-embed) that need a real ID3D11Device* without duplicating this
        /// backend's own device-creation path (NOXNA).
        [[nodiscard]] ID3D11Device* GetDeviceEXT() const { return device_.Get(); }
        /// Exposes the real device context for D3DCommon/tests that need to issue Map/Unmap or
        /// draw calls without duplicating this backend's own context-creation path (NOXNA,
        /// DX-30/DX-31's buffer backends both need this).
        [[nodiscard]] ID3D11DeviceContext* GetContextEXT() const { return context_.Get(); }
        /// Exposes the per-(shader,stride) ID3D11InputLayout cache (NOXNA, DX-32) -- shared by
        /// tests now and by Phase DX8's draw-call wiring later.
        [[nodiscard]] D3D11InputLayoutCache& GetInputLayoutCacheEXT() { return inputLayoutCache_; }

        // ---- IGraphicsBackend: real (Phase DX5) ----
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        // ---- IGraphicsBackend: real (Phase DX6) ----
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;

        // ---- IGraphicsBackend: real (Phase DX7) ----
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /// Exposes the blend/depth-stencil/rasterizer state caches (NOXNA) -- Phase DX8's draw-call
        /// wiring reuses these directly rather than re-implementing the same caching.
        [[nodiscard]] D3D11BlendStateCache& GetBlendStateCacheEXT() { return blendStateCache_; }
        [[nodiscard]] D3D11DepthStencilStateCache& GetDepthStencilStateCacheEXT() { return depthStencilStateCache_; }
        [[nodiscard]] D3D11RasterizerStateCache& GetRasterizerStateCacheEXT() { return rasterizerStateCache_; }

        /// NOXNA (Phase DX6): updates Clear()'s target-RTV/DSV tracking to point at a custom
        /// render target's own views. Called by D3D11RenderTargetBackend::BindAsRenderTarget()/
        /// D3D11RenderTargetCubeBackend::BindAsRenderTargetFace() -- those own the real
        /// OMSetRenderTargets()/viewport call, this only updates what Clear() (and friends) target
        /// next, since D3D11 has no single "currently bound FBO" the backend can query back.
        void TrackCurrentRenderTargetEXT(ID3D11RenderTargetView* const* rtvs, int count, ID3D11DepthStencilView* dsv);
        /// NOXNA (Phase DX6): restores the real back-buffer OM binding + viewport, and Clear()'s
        /// tracking to match. Called by D3D11RenderTargetBackend/D3D11RenderTargetCubeBackend's
        /// own UnbindAsRenderTarget() (after any MSAA resolve / mip regeneration they still need
        /// to do), and internally whenever SetRenderTarget2D(nullptr)/SetRenderTargets(nullptr, 0)
        /// is used to go straight back to the back buffer.
        void RestoreBackBufferRenderTargetEXT();

        // ---- IGraphicsBackend: honest "not yet implemented" stubs (Phase DX8+) ----
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        void CreateDeviceResources();
        void CreateSwapChainResources();
        void CreateWindowSizeDependentViews();
        void ReleaseWindowSizeDependentViews();
        /// DX-29: resize handling -- touches ONLY the window-size group + ResizeBuffers() on the
        /// existing swap chain. Called lazily from Present() when the SDL window size no longer
        /// matches the swap chain's own cached size.
        void EnsureSwapChainSize();
        /// DX-27: device-lost/removed detection (not full automatic recovery yet).
        void CheckDeviceRemoved(HRESULT hr) const;

        SDL_Window* window_ = nullptr;
        int width_ = 0;
        int height_ = 0;

        // Device lifetime (plan_dx.md design decision 11).
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<IDXGIFactory2> factory_;
        bool allowTearingSupported_ = false;
        bool debugLayerEnabled_ = false;
        D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;

        // Swap-chain lifetime.
        ComPtr<IDXGISwapChain1> swapChain_;

        // Window-size lifetime.
        ComPtr<ID3D11Texture2D> backBufferTexture_;
        ComPtr<ID3D11RenderTargetView> backBufferRTV_;
        ComPtr<ID3D11Texture2D> depthStencilTexture_;
        ComPtr<ID3D11DepthStencilView> depthStencilView_;

        // Phase DX6: tracks whatever Clear()/ClearX should actually target -- the back buffer by
        // default, or a custom render target's own views once SetRenderTarget2D/SetRenderTargets
        // binds one (D3D11 has no globally-queryable "current FBO" the way GL does, so this
        // backend must track it explicitly; see TrackCurrentRenderTargetEXT/
        // RestoreBackBufferRenderTargetEXT). Raw, non-owning pointers -- lifetime is owned by
        // backBufferRTV_/depthStencilView_ or by whichever D3D11RenderTargetBackend is currently
        // bound (which outlives the binding, owned by the caller's RenderTarget2D/Cube).
        ID3D11RenderTargetView* currentColorRTVs_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        int currentRTVCount_ = 0;
        ID3D11DepthStencilView* currentDSV_ = nullptr;
        /// Non-owning; nullptr means the back buffer is the active render target. Used so
        /// SetRenderTarget2D/SetRenderTargets can finalize (MSAA resolve / mip regen) whatever was
        /// previously bound before switching to something else, matching how GraphicsDevice only
        /// ever calls SetRenderTarget2D(nullptr) to signal "go back to the back buffer" rather
        /// than calling the old target's UnbindAsRenderTarget() itself.
        D3D11RenderTargetBackend* currentCustomRT_ = nullptr;

        // Phase DX6 (DX-44): sampler-state cache shared by ApplySamplerState().
        D3D11SamplerCache samplerCache_;

        // Phase DX7 (DX-50/DX-51/DX-52): blend/depth-stencil/rasterizer state caches, plus the
        // currently-bound state objects and the two standalone-settable values (blend factor,
        // reference stencil) that OMSetBlendState()/OMSetDepthStencilState() need re-supplied on
        // every bind but which XNA allows changing independently of a full Apply*State() call
        // (GraphicsDevice.BlendFactor, GraphicsDevice.ReferenceStencil -- Task 870/319).
        D3D11BlendStateCache blendStateCache_;
        D3D11DepthStencilStateCache depthStencilStateCache_;
        D3D11RasterizerStateCache rasterizerStateCache_;
        ComPtr<ID3D11BlendState> currentBlendState_;
        ComPtr<ID3D11DepthStencilState> currentDepthStencilState_;
        float currentBlendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        int currentReferenceStencil_ = 0;

        // Presentation policy (plan_dx.md design decision 13: capability vs. policy, kept separate).
        bool vsyncEnabled_ = true;
        bool allowTearingRequested_ = true;
        bool exclusiveFullscreen_ = false;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;

        // Phase DX5 (DX-32): per-(shader,stride) ID3D11InputLayout cache.
        D3D11InputLayoutCache inputLayoutCache_;
    };
}
