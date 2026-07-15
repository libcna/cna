#pragma once

// plan_dx3.md Phase X1/X2: DX3 backend skeleton + real DirectDraw (via the ../free-direct sibling)
// device/window bring-up. 2D-only, like SDL_RENDERER -- every 3D entry point throws (Phase X7).
//
// This header intentionally does NOT include <ddraw.h> (design decision 9's containment rule),
// for a stronger reason than "matches D3D11/D3D12 precedent": free-direct's <ddraw.h> pulls in
// free-api's <windows.h> compatibility shim, which globally #defines fopen -> free_api_fopen for
// every translation unit that includes it. Unlike D3D11's <d3d11.h> (no such macro), leaking that
// into this header would silently rewrite fopen() in every .cpp across the project that happens to
// include this backend header. All real free-direct/<ddraw.h> usage lives in
// Dx3GraphicsBackend.cpp behind the Impl pimpl below.

#include "../Common/IGraphicsBackend.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace CNA::Internal::Backends::Dx3
{
    /**
     * DX3 graphics backend (plan_dx3.md): a CPU 2D compositor that uses free-direct's
     * IDirectDraw/IDirectDrawSurface as its pixel storage/present mechanism. Real DirectDraw
     * device/window bring-up (Phase X2): DirectDrawCreate -> SetCooperativeLevel (against CNA's
     * own already-existing SDL_Window*) -> SetDisplayMode -> primary CreateSurface. Because
     * free-direct's IDirectDrawSurface::Lock() never exposes a writable pointer for the *primary*
     * surface (verified against free-direct's own src/directdraw/DirectDraw.cpp -- GetSurfaceDesc
     * only sets lpSurface/DDSD_LPSURFACE for SurfaceType::Offscreen), this backend never composites
     * directly onto the primary. Instead it owns a second, Lockable offscreen "shadow backbuffer"
     * surface that Clear()/SpriteBatch draws always target; Present() is a single identity Blt()
     * from the shadow buffer onto the real primary, relying on free-direct's own auto-present-on-
     * dirty-Blt behavior (Flip() is never called -- it would only disable that auto-present path
     * for no benefit, since Flip() itself does not copy from anywhere else).
     *
     * Textures/render targets (Phase X3) and the SpriteBatch CPU compositor (Phase X4/X5) are not
     * yet implemented as of Phase X2 -- CreateTexture()/CreateSpriteBatch() throw honestly.
     */
    class Dx3GraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit Dx3GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~Dx3GraphicsBackend() override;

        Dx3GraphicsBackend(const Dx3GraphicsBackend&) = delete;
        Dx3GraphicsBackend& operator=(const Dx3GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: real (Phase X2) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        SDL_Window* GetWindowInternal() const override;
        // free-direct manages its own internal SDL_Renderer privately (created lazily inside
        // PresentPrimary, against the same SDL_Window* this backend hands to
        // SetCooperativeLevel) -- it is never exposed to CNA, so this always returns nullptr,
        // same as every other non-SDL_Renderer-based backend.
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        // ---- IGraphicsBackend: not yet implemented (Phase X3/X4) ----
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        // ---- 3D pipeline: NOT supported by DX3 (DirectDraw is 2D-only). ----
        // @note Status: STUB. Every entry point throws std::runtime_error (Phase X7).
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
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
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
