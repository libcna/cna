#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaGeneratedBlender.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaMeshEffectRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaOwnership.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRasterState.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaStartupDiagnostic.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"

#include "include/core/SkBlender.h"
#include "include/core/SkBlendMode.h"

#include <functional>
#include <memory>

namespace CNA::Internal::Renderers::Skia
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
     * First functional SKIA renderer slice: a platform-presented Skia raster backbuffer.
     *
     * This deliberately contains no EasyGL calls or GL context. Unsupported resource/draw paths
     * throw clearly until their own Skia implementation tasks land; see plan_skia.md.
     */
    class SkiaRenderer final : public IGraphicsRenderer
    {
    public:
        explicit SkiaRenderer(
            const GraphicsRendererCreateArgs& args,
            SkiaInitializationFailurePointEXT failurePoint =
                SkiaInitializationFailurePointEXT::None);
        ~SkiaRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /// Returns bounded live-resource counters for raster-renderer diagnostics and debug tests.
        CNAEXT [[nodiscard]] SkiaResourceStats GetResourceStatsEXT() const noexcept
        {
            return resourceCounters_ ? resourceCounters_->GetStats() : SkiaResourceStats{};
        }
        /// Returns the exact stable line emitted once after successful renderer construction.
        CNAEXT [[nodiscard]] std::string_view GetStartupDiagnosticEXT() const noexcept
        {
            return kSkiaStartupDiagnostic;
        }
        /// Actual presenter interval after the boolean-vsync service clamp (2 becomes 1).
        CNAEXT [[nodiscard]] int GetSwapIntervalEXT() const noexcept { return swapInterval_; }
        /// Deterministic minimized/zero-output seam; does not alter the real platform window.
        CNAEXT void DebugSetPresentationOutputSizeEXT(int width, int height);
        CNAEXT void DebugClearPresentationOutputSizeEXT();

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int width, int height, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(
            const std::string& vertSrc, const std::string& fragSrc) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int width, int height, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /// SKIA-142: the live construction path, carrying an explicit SurfaceFormat (as `int`,
        /// matching CreateTexture's ImageData convention). CreateRenderTarget2D above forwards
        /// here with Color for source/ABI compatibility with any existing direct caller.
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int width, int height, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* renderTarget) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* renderTarget, int face) override;
        void ReadBackbuffer(int x, int y, int width, int height, std::uint8_t* pixels) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        void SetScissorRect(int x, int y, int width, int height) override;
        void SetViewport(int x, int y, int width, int height, float minDepth, float maxDepth) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        // plan_runtimerenderer.md design decision 9: Skia stores each promoted format in its own
        // native layout, so unlike every other renderer it has real answers about which formats a
        // texture may use and which of them a Color* transfer can meaningfully read. These used to
        // live as #ifdef CNA_RENDERER_SKIA blocks inside Texture2D.cpp and RenderTarget2D.cpp.

        /**
         * @brief Whether a Texture2D may be created with the given surface format.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Supported when Skia stores that format natively, Unsupported otherwise.
         */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(int surfaceFormat) const override;

        /**
         * @brief Whether a Color* transfer reads that format's real bits.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Supported only for the genuinely 32-bit RGBA-shaped formats.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(int surfaceFormat) const override;

        /**
         * @brief Whether a RenderTarget2D may use this format.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Supported for the formats real XNA/FNA hardware reports renderable.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(int surfaceFormat) const override;

        /**
         * @brief Whether the format transfers as compressed blocks rather than pixels.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true for the block-compressed formats Skia stores natively.
         */
        [[nodiscard]] bool IsCompressedTransferFormatEXT(int surfaceFormat) const override;
        void Ensure3DSupported(const char* operation) const override;

        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void SetReferenceStencil(int value) override;
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

    private:
        void RecreateBackbuffer(int requestedWidth, int requestedHeight);
        void GetPresentationOutputSize(int& width, int& height) const;
        void RefreshDynamicBackbufferIfNeeded();
        void RecreatePresentationRenderer();
        void ApplyLogicalPresentation();
        void AssertOwnership(const char* operation) const;
        struct PresentationViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };
        [[nodiscard]] PresentationViewport ComputePresentationViewport() const;
        [[nodiscard]] SkiaSurface& ActiveSurface();
        [[nodiscard]] const SkiaSurface& ActiveSurface() const;
        [[nodiscard]] int LogicalWidth() const noexcept { return surface_.Width(); }
        [[nodiscard]] int LogicalHeight() const noexcept { return surface_.Height(); }

        RendererSurfaceInfo surfaceInfo_;
        CNA::Platform::IPlatformSurfacePresenter* presenter_ = nullptr;
        std::function<void(RendererDeviceEvent)> deviceEventCallback_;
        SkiaSurface surface_;
        std::shared_ptr<SkiaOwnership> ownership_ = std::make_shared<SkiaOwnership>();
        std::shared_ptr<SkiaRenderTargetBinding> targetBinding_
            = std::make_shared<SkiaRenderTargetBinding>(ownership_);
        std::shared_ptr<SkiaResourceCounters> resourceCounters_ = std::make_shared<SkiaResourceCounters>();
        /** SKIA-157: one persistent mesh-effect compilation cache per graphics renderer instance,
         * shared across every `CNA_SKIA_SKSL_MESH_V1`-tagged `ShaderEffect` this renderer compiles. */
        SkiaMeshEffectCacheEXT meshEffectCache_;
        SkBlendMode spriteBlendMode_ = SkBlendMode::kSrcOver;
        sk_sp<SkBlender> spriteCustomBlender_;
        SkiaSourceAlphaConvention spriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
        SkBlendMode configuredSpriteBlendMode_ = SkBlendMode::kSrcOver;
        sk_sp<SkBlender> configuredSpriteCustomBlender_;
        SkiaSourceAlphaConvention configuredSpriteSourceAlphaConvention_
            = SkiaSourceAlphaConvention::Premultiplied;
        SkiaGeneratedBlendSelectors configuredGeneratedBlendSelectors_;
        std::array<float, 4> blendFactor_ = {1.0f, 1.0f, 1.0f, 1.0f};
        int configuredColorWriteMask_ = 15;
        bool configuredUsesGeneratedBlender_ = false;
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
} // namespace CNA::Internal::Renderers::Skia
