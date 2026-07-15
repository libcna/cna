#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Backends::Canvas
{
    /**
     * @brief HTML Canvas 2D graphics backend (Emscripten-only).
     *
     * See plan_canvas.md for the full task breakdown and design rationale. Window/viewport
     * bookkeeping (C1), Clear/Present (C2), textures/render targets (C3), and SpriteBatch (C4) are
     * all real. Blend/sampler state mapping (C5) and SpriteFont (C6, expected to need no new code)
     * are still open. The inherently-3D-only pure virtuals (ClearColorAndDepth and friends,
     * vertex/index buffers, DrawColoredPrimitives) throw via ThrowNo3D() -- permanent behavior, not
     * a placeholder; Phase C7 just audits/formalizes this same wiring across the full 3D surface.
     */
    class CanvasGraphicsBackend final : public IGraphicsBackend
    {
    public:
        CanvasGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                               CnaPresentationMode mode);
        ~CanvasGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        // plan_canvas.md CANVAS-26: a Canvas2D context is inherently single-target (same
        // conclusion SDL_RENDERER's Task 709 reached) -- throws for count > 1.
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        // plan_canvas.md CANVAS-40/41: maps the 4 standard BlendState presets to
        // globalCompositeOperation (Design decision 5); throws for any other Blend/BlendFunction
        // combination -- Canvas2D has no generic blend-factor/equation model to fall back on.
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;

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

        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        // Derives the logical (virtual) viewport size from the real canvas/window's physical
        // pixel size and virtualWidth_/virtualHeight_/presentationMode_ -- same FixedHeightDynamicWidth
        // math EasyGLGraphicsBackend::getLogicalSize() uses (plan_canvas.md CANVAS-13: this math is
        // backend-agnostic, only the underlying physical-size query is backend-specific, and here
        // that query is just SDL_GetWindowSize() since SDL3 already keeps the DOM <canvas> element's
        // width/height attributes in sync with the SDL_Window it backs on Emscripten).
        void getLogicalSize(int& width, int& height) const;

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
    };
}
