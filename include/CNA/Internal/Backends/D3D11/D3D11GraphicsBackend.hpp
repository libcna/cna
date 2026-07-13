#pragma once

// plan_dx.md Phase DX2/DX4: D3D11 backend skeleton + device/swap-chain/back-buffer.
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_BACKEND=D3D11).

#include "../Common/IGraphicsBackend.hpp"

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D11
{
    using Microsoft::WRL::ComPtr;

    /**
     * D3D11 graphics backend (plan_dx.md). Implements IGraphicsBackend on top of Direct3D 11 via
     * DXGI, with real device/swap-chain/back-buffer/clear/present/readback (Phase DX4) and honest
     * "not yet implemented" stubs for everything Phase DX5 onward will add (vertex/index buffers,
     * textures, draw calls, SpriteBatch) -- mirrors plan_software.md/plan_headless.md's own
     * "CnaTests must link cleanly even before most methods are real" bar.
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

        // ---- IGraphicsBackend: honest "not yet implemented" stubs (Phase DX5+) ----
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
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

        // Presentation policy (plan_dx.md design decision 13: capability vs. policy, kept separate).
        bool vsyncEnabled_ = true;
        bool allowTearingRequested_ = true;
        bool exclusiveFullscreen_ = false;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
    };
}
