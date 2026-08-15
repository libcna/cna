#pragma once

#include "CNA/Internal/Renderers/Gdi/GdiConfiguration.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiPresentation.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::Gdi
{
    class GdiSoftware2DCore;

    struct GdiFramebufferStorageTelemetry
    {
        std::size_t colorBytes = 0;
        std::size_t depthBytes = 0;
        std::size_t stencilBytes = 0;
        std::size_t multiSampleBytes = 0;

        [[nodiscard]] std::size_t TotalBytes() const
        {
            return colorBytes + depthBytes + stencilBytes + multiSampleBytes;
        }
    };

    /**
     * @brief Win32 GDI presentation renderer for CNA's 2D API.
     *
     * SpriteBatch, textures and 2D render targets are CPU-rasterized by the reusable Software
     * core. Present() transfers the RGBA8 backbuffer to the platform window's HWND with Win32 DIB APIs:
     * SetDIBitsToDevice for a 1:1 blit and StretchDIBits when scaling is necessary. This makes the
     * final display operation real Win32 GDI rather than a GPU API. The reusable
     * rasterizer is privately composed behind explicit 2D forwards; GDI derives directly from the
     * renderer interface, and every 3D/resource entry is deliberately rejected.
     */
    class GdiRenderer final : public IGraphicsRenderer
    {
    public:
        explicit GdiRenderer(const GraphicsRendererCreateArgs& args);
        /** @brief Internal deterministic-construction overload used by focused renderer tests. */
        GdiRenderer(const GraphicsRendererCreateArgs& args, GdiConfiguration configuration);
        ~GdiRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void OnSurfaceInvalidated(CNA::Platform::WindowId window) override;
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        void UpdatePresentationFormatEXT(int, int, bool) override {}
        [[nodiscard]] int GetMultiSampleCount() const override;
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int) const override { return 0; }
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int) const override { return 0; }
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass,
                                    int stencilFail, int stencilDepthFail, int stencilMask,
                                    int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail,
                                    int ccwStencilDepthFail) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /** @brief Test telemetry for the currently accumulated logical backbuffer damage. */
        [[nodiscard]] bool DebugGetBackbufferDamage(Rectangle& rectangle,
                                                    bool& fullyDirty) const;
        /** @brief Clears damage telemetry without issuing a native present; intended for tests. */
        void DebugResetBackbufferDamage();
        /** @brief Whether a platform invalidation still requires a complete repaint. */
        [[nodiscard]] bool DebugIsNativeClientInvalidated() const;
        /** @brief Makes the next DIB blit report zero lines, exercising failure retention. */
        void DebugForceNextDibBlitFailure() { debugForceNextDibBlitFailure_ = true; }
        /** @brief Makes the next Present()'s checked ReleaseDC report failure (GDI-077). */
        void DebugForceNextReleaseDcFailure() { debugForceNextReleaseDcFailure_ = true; }
        /** @brief Returns the most recent presentation plan/result telemetry. */
        [[nodiscard]] bool DebugGetLastPresentationTelemetry(
            GdiPresentationTelemetry& telemetry) const;
        /** @brief Reports actual vector storage for GDI-067 allocation-contract tests. */
        [[nodiscard]] GdiFramebufferStorageTelemetry DebugGetBackbufferStorage() const;
        /** @brief Returns the construction-time policy snapshot for focused renderer tests. */
        [[nodiscard]] GdiConfiguration DebugGetConfiguration() const { return configuration_; }

        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* target) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* target, int face) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h,
                         float minDepth, float maxDepth) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsDepthBuffer() const override { return false; }
        [[nodiscard]] bool SupportsStencilBuffer() const override { return true; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        [[nodiscard]] int GetMaxTextureDimension() const override;

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
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib, const Matrix& world,
                                          const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                              const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib, const Matrix& world,
                                     const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib, const Matrix& world,
                                       const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount, const GpuDrawParams& params) override;

        void SetContextRecoveryEnabled(bool) override {}
        void SetStringMarkerEXT(const char*) override {}
        void DebugSimulateContextLoss() override {}
        void DebugRestoreContext() override {}

    protected:
        void OnSpriteRasterBounds(int minX, int minY, int maxX, int maxY);

    private:
        void RecordNativeClientInvalidation();
        void SynchronizeBackbufferSize();
        void GetLogicalSize(int& width, int& height) const;
        bool GetDrawablePixelSize(int& width, int& height) const;
        bool GetWindowCoordinateSize(int& width, int& height) const;
        void MarkBackbufferDirty(const Rectangle& rectangle);
        void MarkBackbufferFullyDirty();
        void ResetBackbufferDamage();

        std::unique_ptr<GdiSoftware2DCore> software2D_;
        PlatformRendererSurfaceState surface_;
        void* nativeWindow_ = nullptr;
        int requestedVirtualWidth_ = 0;
        int requestedVirtualHeight_ = 0;
        std::atomic<std::uint64_t> nativeInvalidationGeneration_{1};
        std::uint64_t presentedNativeInvalidationGeneration_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        const GdiConfiguration configuration_{};
        bool renderingToBackbuffer_ = true;
        bool backbufferFullyDirty_ = true;
        bool backbufferDirtyValid_ = true;
        int backbufferDirtyX_ = 0;
        int backbufferDirtyY_ = 0;
        int backbufferDirtyWidth_ = 0;
        int backbufferDirtyHeight_ = 0;
        bool debugForceNextDibBlitFailure_ = false;
        bool debugForceNextReleaseDcFailure_ = false;
        GdiPresentationTelemetry lastPresentationTelemetry_{};
    };
} // namespace CNA::Internal::Renderers::Gdi
