#pragma once

#include "CNA/Internal/Backends/Gdi/GdiPresentation.hpp"
#include "CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp"

#include <SDL3/SDL_events.h>

#include <atomic>
#include <cstdint>

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
        [[nodiscard]] int GetMultiSampleCount() const override;
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

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        /** @brief Test telemetry for the currently accumulated logical backbuffer damage. */
        [[nodiscard]] bool DebugGetBackbufferDamage(Rectangle& rectangle,
                                                    bool& fullyDirty) const;
        /** @brief Clears damage telemetry without issuing a native present; intended for tests. */
        void DebugResetBackbufferDamage();
        /** @brief Whether a watched native-window event still requires a complete repaint. */
        [[nodiscard]] bool DebugIsNativeClientInvalidated() const;
        /** @brief Makes the next DIB blit report zero lines, exercising failure retention. */
        void DebugForceNextDibBlitFailure() { debugForceNextDibBlitFailure_ = true; }
        /** @brief Returns the most recent presentation plan/result telemetry. */
        [[nodiscard]] bool DebugGetLastPresentationTelemetry(
            GdiPresentationTelemetry& telemetry) const;

        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* target) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsStencilBuffer() const override { return true; }
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

    protected:
        void OnSpriteRasterBounds(int minX, int minY, int maxX, int maxY) override;

    private:
        static bool SDLCALL WindowEventWatch(void* userdata, SDL_Event* event);
        void RecordNativeClientInvalidation();
        void SynchronizeBackbufferSize();
        void GetLogicalSize(int& width, int& height) const;
        bool GetClientSize(int& width, int& height) const;
        void MarkBackbufferDirty(const Rectangle& rectangle);
        void MarkBackbufferFullyDirty();
        void ResetBackbufferDamage();

        SDL_Window* window_ = nullptr;
        void* nativeWindow_ = nullptr;
        int requestedVirtualWidth_ = 0;
        int requestedVirtualHeight_ = 0;
        std::uint32_t windowId_ = 0;
        bool eventWatchRegistered_ = false;
        std::atomic<std::uint64_t> nativeInvalidationGeneration_{1};
        std::uint64_t presentedNativeInvalidationGeneration_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        bool renderingToBackbuffer_ = true;
        bool backbufferFullyDirty_ = true;
        bool backbufferDirtyValid_ = true;
        int backbufferDirtyX_ = 0;
        int backbufferDirtyY_ = 0;
        int backbufferDirtyWidth_ = 0;
        int backbufferDirtyHeight_ = 0;
        bool debugForceNextDibBlitFailure_ = false;
        GdiPresentationTelemetry lastPresentationTelemetry_{};
    };
} // namespace CNA::Internal::Backends::Gdi
