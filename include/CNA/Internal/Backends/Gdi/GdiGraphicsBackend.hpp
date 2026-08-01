#pragma once

#include "CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Gdi
{
    /**
     * @brief Win32 GDI presentation backend for CNA's 2D API.
     *
     * SpriteBatch, textures and 2D render targets are CPU-rasterized by the reusable Software
     * core. Present() transfers the RGBA8 backbuffer to the SDL window's HWND with Win32 DIB APIs:
     * SetDIBitsToDevice for a 1:1 blit and StretchDIBits when scaling is necessary. This makes the
     * final display operation real Win32 GDI rather than SDL_Renderer or a GPU API. The 3D-facing
     * Software operations are deliberately overridden to throw: GDI exposes a 2D-only contract
     * even though its CPU implementation is shared with the software rasterizer.
     */
    class GdiGraphicsBackend final : public Software::SoftwareGraphicsBackend
    {
    public:
        GdiGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                           CnaPresentationMode presentationMode);
        ~GdiGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                       int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int indexCapacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib, const Matrix& world,
                                          const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferBackend& vb, const Matrix& world,
                              const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                     const IIndexBufferBackend& ib, const Matrix& world,
                                     const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb,
                                       const IIndexBufferBackend& ib, const Matrix& world,
                                       const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount, const GpuDrawParams& params) override;

    private:
        void SynchronizeBackbufferSize();
        void GetLogicalSize(int& width, int& height) const;
        bool GetClientSize(int& width, int& height) const;

        SDL_Window* window_ = nullptr;
        void* nativeWindow_ = nullptr;
        int requestedVirtualWidth_ = 0;
        int requestedVirtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
    };
} // namespace CNA::Internal::Backends::Gdi
