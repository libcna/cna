#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Backends::Canvas
{
    /// Composite-op codes CNA_Canvas2D_SetCompositeOp (CanvasGraphicsBackend.cpp) understands.
    /// AlphaBlend and NonPremultiplied are DELIBERATELY kept distinct (not both "SourceOver"): both
    /// resolve to the same ctx.globalCompositeOperation ('source-over'), but AlphaBlend's real
    /// contract (srcBlend=One) assumes the source color is already premultiplied by its own alpha --
    /// the SDL_RENDERER backend has a dedicated pixel test (Task 697) constructing genuinely
    /// premultiplied source data specifically to verify this, so a Canvas2D draw under AlphaBlend
    /// must un-premultiply that data first (see CanvasSpriteBatchBackend.cpp's per-pixel pass),
    /// since Canvas2D's own 'source-over' always treats its input as straight alpha.
    enum class CanvasCompositeOp { Copy = 0, NonPremultipliedSourceOver = 1, AlphaBlendSourceOver = 2, Lighter = 3 };

    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsBackend::ApplyBlendState's
    /// own parameter doc) to a CanvasCompositeOp; throws std::runtime_error for any Blend/BlendFunction
    /// combination that isn't one of the 4 standard presets (Design decision 5). Contains no EM_JS/JS
    /// calls -- exposed standalone so plan_canvas.md CANVAS-80's structural GTest coverage can unit
    /// test this mapping directly, without needing a real CanvasRenderingContext2D.
    CanvasCompositeOp BlendStateToCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief HTML Canvas 2D graphics backend (Emscripten-only).
     *
     * See plan_canvas.md for the full task breakdown and design rationale. Window/viewport
     * bookkeeping (C1), Clear/Present (C2), textures/render targets (C3), SpriteBatch incl.
     * SpriteFont (C4/C6), and blend/sampler state mapping (C5) are all real. The inherently-3D-only
     * pure virtuals (ClearColorAndDepth and friends, vertex/index buffers, DrawColoredPrimitives)
     * throw via ThrowNo3D(); the rest of the 3D-only surface (Draw*Ex/DrawInstancedPrimitivesEx,
     * CreateIndexBuffer32, Create{Texture3D,TextureCube,RenderTargetCube,OcclusionQuery,
     * EffectBackend}) is intentionally left on IGraphicsBackend's own shared defaults, which already
     * do the right thing here (throw-by-delegation or return nullptr) without needing a Canvas-local
     * override -- confirmed against SDL_RENDERER's own precedent of not overriding these either
     * (Phase C7).
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
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        // plan_canvas.md CANVAS-65: no Canvas2D target -- main canvas or off-screen -- ever has a
        // real depth/stencil buffer (same reasoning as IRenderTargetBackend::HasRealDepthBuffer's
        // override, CANVAS-23), same as SDL_RENDERER's own override for the same reason.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        // Every 3D call below unconditionally throws. SupportsCapability() lets callers check
        // ahead of time instead of relying on the throw -- see CNA::GraphicsCapability.
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability /*capability*/) const override
        {
            // 2D-only by design: none of the capabilities CNA::GraphicsCapability currently
            // enumerates are supported on this backend.
            return false;
        }

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
