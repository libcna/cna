#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers::Canvas
{
    /// Composite-op codes CNA_Canvas2D_SetCompositeOp (CanvasRenderer.cpp) understands.
    /// AlphaBlend and NonPremultiplied are DELIBERATELY kept distinct (not both "SourceOver"): both
    /// resolve to the same ctx.globalCompositeOperation ('source-over'), but AlphaBlend's real
    /// contract (srcBlend=One) assumes the source color is already premultiplied by its own alpha --
    /// the native 2D renderer has a dedicated pixel test (Task 697) constructing genuinely
    /// premultiplied source data specifically to verify this, so a Canvas2D draw under AlphaBlend
    /// must un-premultiply that data first (see CanvasSpriteBatchRenderer.cpp's cached texture
    /// variant), since Canvas2D's own 'source-over' always treats its input as straight alpha.
    enum class CanvasCompositeOp { Copy = 0, NonPremultipliedSourceOver = 1, AlphaBlendSourceOver = 2, Lighter = 3 };

    /** @brief Blend state shared by one Canvas renderer and the SpriteBatch instances it creates. */
    struct CanvasRendererState
    {
        /** @brief Composite operation captured by a SpriteBatch when it begins. */
        CanvasCompositeOp compositeOp = CanvasCompositeOp::AlphaBlendSourceOver;
    };

    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsRenderer::ApplyBlendState's
    /// own parameter doc) to a CanvasCompositeOp; throws std::runtime_error for any Blend/BlendFunction
    /// combination that isn't one of the 4 standard presets (Design decision 5). Contains no EM_JS/JS
    /// calls -- exposed standalone so plan_canvas.md CANVAS-80's structural GTest coverage can unit
    /// test this mapping directly, without needing a real CanvasRenderingContext2D.
    CanvasCompositeOp BlendStateToCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief HTML Canvas 2D graphics renderer (Emscripten-only).
     *
     * See plan_canvas.md for the full task breakdown and design rationale. Window/viewport
     * bookkeeping (C1), Clear/Present (C2), textures/render targets (C3), SpriteBatch incl.
     * SpriteFont (C4/C6), and blend/sampler state mapping (C5) are all real. The inherently-3D-only
     * pure virtuals (ClearColorAndDepth and friends, vertex/index buffers, DrawColoredPrimitives)
     * preserve their established throw/null behavior by default.
     * Unsupported3DGraphicsCallBehavior::WarnAndStub turns them into warn-once no-ops and
     * supplies safe null-object resources.
     */
    class CanvasRenderer final : public IGraphicsRenderer
    {
    public:
        explicit CanvasRenderer(const GraphicsRendererCreateArgs& args);
        ~CanvasRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        // plan_canvas.md CANVAS-26: a Canvas2D context is inherently single-target (same
        // conclusion the native 2D renderer's Task 709 reached) -- throws for count > 1.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        // plan_canvas.md CANVAS-40/41: maps the 4 standard BlendState presets to
        // globalCompositeOperation (Design decision 5); throws for any other Blend/BlendFunction
        // combination -- Canvas2D has no generic blend-factor/equation model to fall back on.
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        // plan_canvas.md CANVAS-65: no Canvas2D target -- main canvas or off-screen -- ever has a
        // real depth/stencil buffer (same reasoning as IRenderTargetRenderer::HasRealDepthBuffer's
        // override, CANVAS-23), same as the native 2D renderer's override for the same reason.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        // SupportsCapability() lets callers check ahead of time; the policy controls what
        // happens if an unsupported call is nevertheless made.
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // Canvas is 2D-only, but its `lighter` composite operation is the renderer's explicit
            // and tested mapping for BlendState::Additive.
            return capability == CNA::GraphicsCapability::AdditiveBlending;
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

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        // Derives the logical (virtual) viewport size from the real canvas/window's client
        // pixel size and virtualWidth_/virtualHeight_/presentationMode_ -- same FixedHeightDynamicWidth
        // math EasyGLRenderer::getLogicalSize() uses (plan_canvas.md CANVAS-13): this math is
        // renderer-agnostic; the platform snapshot supplies the drawable size and density.
        void getLogicalSize(int& width, int& height) const;
        void getWindowSize(int& width, int& height) const;

        RendererSurfaceInfo surface_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        std::shared_ptr<CanvasRendererState> state_ = std::make_shared<CanvasRendererState>();
    };
}
