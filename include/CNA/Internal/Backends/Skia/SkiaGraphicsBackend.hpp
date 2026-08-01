#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <SDL3/SDL.h>
#include "include/core/SkBlendMode.h"

#include <memory>

namespace CNA::Internal::Backends::Skia
{
    /**
     * First functional SKIA backend slice: an SDL-presented Skia raster backbuffer.
     *
     * This deliberately contains no EasyGL calls or GL context. Unsupported resource/draw paths
     * throw clearly until their own Skia implementation tasks land; see plan_skia.md.
     */
    class SkiaGraphicsBackend final : public IGraphicsBackend
    {
    public:
        SkiaGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                            CnaPresentationMode presentationMode, int swapInterval);
        ~SkiaGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return renderer_; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int width, int height, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* renderTarget) override;
        void ReadBackbuffer(int x, int y, int width, int height, std::uint8_t* pixels) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        void RecreateBackbuffer(int requestedWidth, int requestedHeight);
        void ApplyLogicalPresentation();
        [[nodiscard]] SkiaSurface& ActiveSurface() noexcept { return *activeSurface_; }
        [[nodiscard]] const SkiaSurface& ActiveSurface() const noexcept { return *activeSurface_; }
        [[nodiscard]] int LogicalWidth() const noexcept { return surface_.Width(); }
        [[nodiscard]] int LogicalHeight() const noexcept { return surface_.Height(); }

        SDL_Window* window_ = nullptr;
        SDL_Renderer* renderer_ = nullptr;
        SDL_Texture* presentTexture_ = nullptr;
        SkiaSurface surface_;
        SkiaSurface* activeSurface_ = &surface_;
        SkBlendMode spriteBlendMode_ = SkBlendMode::kSrcOver;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int preferredVirtualHeight_ = 0;
    };
} // namespace CNA::Internal::Backends::Skia
