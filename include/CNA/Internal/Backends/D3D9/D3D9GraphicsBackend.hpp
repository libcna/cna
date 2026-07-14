#pragma once

// plan_dx9.md Phase D9-1 (D9-11): D3D9 backend skeleton. No device exists yet (that is D9-30,
// Phase D9-3) -- every substantive IGraphicsBackend method throws NotYetImplemented() for now.
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_BACKEND=D3D9).
//
// Unlike D3D11/D3D12, this backend does not use D3DCommon (plan_dx9.md design decision 12 --
// D3DFORMAT is a different enum space from DXGI_FORMAT, and D3D9 has no state objects at all).

#include "../Common/IGraphicsBackend.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D9
{
    using Microsoft::WRL::ComPtr;

    /**
     * D3D9 graphics backend (plan_dx9.md). Implements IGraphicsBackend on top of plain Direct3D 9
     * (Direct3DCreate9, not D3D9Ex -- design decision 2), targeting Microsoft's own XNA 4.0 Stock
     * Effects HLSL compiled to real vs_2_0/ps_2_0 bytecode (design decision 3/5), with the
     * project's stated goal of pixel-for-pixel indistinguishability from the original XNA 4.0
     * runtime, not mere feature parity.
     *
     * This is the Phase D9-1 skeleton: no device exists yet. Device/swap-chain creation is D9-30
     * (Phase D9-3), now unblocked by the project owner's approved additive extension to
     * GraphicsBackendCreateArgs/IGraphicsBackend (see plan_dx9.md's "IGraphicsBackend boundary
     * problem" section).
     */
    class D3D9GraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit D3D9GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~D3D9GraphicsBackend() override;

        D3D9GraphicsBackend(const D3D9GraphicsBackend&) = delete;
        D3D9GraphicsBackend& operator=(const D3D9GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: pure virtual, real (trivial bookkeeping; no device needed) ----
        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;

        // ---- IGraphicsBackend: pure virtual, NotYetImplemented until D9-30/D9-31 land ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
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
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        // ---- IGraphicsBackend: silently-empty-default virtuals, explicitly loud until real ----
        // (D9-11's own distinction: these 10 have a `{}` default on IGraphicsBackend itself, so an
        // un-overridden call would silently no-op instead of failing -- override each so a missing
        // capability is a build-time-enforced, loud runtime failure instead.)
        void SetSwapInterval(int interval) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        void SetContextRecoveryEnabled(bool enabled) override;
        void SetStringMarkerEXT(const char* marker) override;
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;

        // Every other IGraphicsBackend virtual (~25 of them, e.g. ReadBackbuffer,
        // CreateOcclusionQuery, CreateTexture3D, ApplyBlendState) already has a throwing or
        // harmlessly-inert default on the base interface -- left un-overridden here on purpose
        // (plan_dx9.md D9-11): inheriting "throws" is fine, only inheriting silence is the trap.

    private:
        SDL_Window* window_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        int presentationMode_ = 0;
    };
}
