#pragma once

// plan_dx.md Phase DX12 (DX-101): D3D12 backend skeleton -- CMake wiring only. Every method below
// is an honest "not yet implemented" stub; DX-102 onward (device/command queue/swap chain,
// off-screen-first per DX-100's own finding) will make this real. Windows-only (see
// CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_BACKEND=D3D12).

#include "../Common/IGraphicsBackend.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    /**
     * D3D12 graphics backend skeleton (plan_dx.md Phase DX12). Implements every
     * IGraphicsBackend pure virtual with an honest "not yet implemented" throw -- mirrors the
     * exact bootstrapping shape D3D11's own very first skeleton used (plan_dx.md Phase DX2,
     * commit 482057ec) before Phase DX4 made it real. DX-100's own spike found the D3D12
     * device/command-queue/command-list/fence path works well locally under Wine+vkd3d-proton,
     * but CreateSwapChainForHwnd crashes in vanilla Wine's dxgi.dll -- so unlike D3D11, whoever
     * lands DX-102 onward should build around off-screen/swap-chain-free proof first and defer
     * swap-chain/Present() verification to a real-Windows/CI-VM pass much earlier than D3D11's
     * own DX-90 did.
     */
    class D3D12GraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit D3D12GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~D3D12GraphicsBackend() override;

        D3D12GraphicsBackend(const D3D12GraphicsBackend&) = delete;
        D3D12GraphicsBackend& operator=(const D3D12GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: honest "not yet implemented" stubs (DX-102 onward) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        [[noreturn]] static void NotYetImplemented(const char* what);

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
    };
}
