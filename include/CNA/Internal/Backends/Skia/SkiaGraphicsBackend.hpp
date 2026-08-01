#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaOwnership.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRasterState.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Backends/Skia/SkiaStartupDiagnostic.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <SDL3/SDL.h>
#include "include/core/SkBlender.h"
#include "include/core/SkBlendMode.h"

#include <functional>
#include <memory>

namespace CNA::Internal::Backends::Skia
{
    /// Deterministic internal constructor-failure seams used to prove transactional cleanup.
    enum class SkiaInitializationFailurePointEXT
    {
        None,
        AfterRenderer,
        AfterBackbuffer,
        AfterRegistration
    };

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
                            CnaPresentationMode presentationMode, int swapInterval,
                            std::function<void(BackendDeviceEvent)> deviceEventCallback = {},
                            SkiaInitializationFailurePointEXT failurePoint =
                                SkiaInitializationFailurePointEXT::None);
        ~SkiaGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return renderer_; }
        /// Returns bounded live-resource counters for raster-backend diagnostics and debug tests.
        NOXNA [[nodiscard]] SkiaResourceStats GetResourceStatsEXT() const noexcept
        {
            return resourceCounters_ ? resourceCounters_->GetStats() : SkiaResourceStats{};
        }
        /// Returns the exact stable line emitted once after successful backend construction.
        NOXNA [[nodiscard]] std::string_view GetStartupDiagnosticEXT() const noexcept
        {
            return kSkiaStartupDiagnostic;
        }
        /// Actual SDL presenter interval after any supported-driver clamp (2 may become 1).
        NOXNA [[nodiscard]] int GetSwapIntervalEXT() const noexcept { return swapInterval_; }
        /// Deterministic minimized/zero-output seam; does not alter the real SDL window.
        NOXNA void DebugSetPresentationOutputSizeEXT(int width, int height);
        NOXNA void DebugClearPresentationOutputSizeEXT();

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(
            int width, int height, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(
            const std::string& vertSrc, const std::string& fragSrc) override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int width, int height, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* renderTarget) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* renderTarget, int face) override;
        void ReadBackbuffer(int x, int y, int width, int height, std::uint8_t* pixels) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        void SetScissorRect(int x, int y, int width, int height) override;
        void SetViewport(int x, int y, int width, int height, float minDepth, float maxDepth) override;

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
        void GetPresentationOutputSize(int& width, int& height) const;
        void RefreshDynamicBackbufferIfNeeded();
        void RecreatePresentationRenderer();
        void RecreatePresentationTexture();
        void DestroyPresentationTexture() noexcept;
        void ApplyLogicalPresentation();
        void AssertOwnership(const char* operation) const;
        [[nodiscard]] SkiaSurface& ActiveSurface();
        [[nodiscard]] const SkiaSurface& ActiveSurface() const;
        [[nodiscard]] int LogicalWidth() const noexcept { return surface_.Width(); }
        [[nodiscard]] int LogicalHeight() const noexcept { return surface_.Height(); }

        SDL_Window* window_ = nullptr;
        SDL_Renderer* renderer_ = nullptr;
        SDL_Texture* presentTexture_ = nullptr;
        std::function<void(BackendDeviceEvent)> deviceEventCallback_;
        SkiaSurface surface_;
        std::shared_ptr<SkiaOwnership> ownership_ = std::make_shared<SkiaOwnership>();
        std::shared_ptr<SkiaRenderTargetBinding> targetBinding_
            = std::make_shared<SkiaRenderTargetBinding>(ownership_);
        std::shared_ptr<SkiaResourceCounters> resourceCounters_ = std::make_shared<SkiaResourceCounters>();
        SkBlendMode spriteBlendMode_ = SkBlendMode::kSrcOver;
        sk_sp<SkBlender> spriteCustomBlender_;
        SkiaSourceAlphaConvention spriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
        SkBlendMode configuredSpriteBlendMode_ = SkBlendMode::kSrcOver;
        sk_sp<SkBlender> configuredSpriteCustomBlender_;
        SkiaSourceAlphaConvention configuredSpriteSourceAlphaConvention_
            = SkiaSourceAlphaConvention::Premultiplied;
        bool blendEnabled_ = true;
        SkiaRasterState rasterState_;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int preferredVirtualWidth_ = 0;
        int preferredVirtualHeight_ = 0;
        int swapInterval_ = 1;
        bool debugOutputSizeOverride_ = false;
        int debugOutputWidth_ = 0;
        int debugOutputHeight_ = 0;
    };
} // namespace CNA::Internal::Backends::Skia
