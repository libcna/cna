#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers::PixiJs
{
    /// Blend-mode codes CNA_PixiJs_SetBlendMode (PixiJsRenderer.cpp) understands. Kept as a
    /// dedicated enum (mirroring CanvasCompositeOp's own reasoning) rather than passing the raw
    /// PIXI.BLEND_MODES numeric value across the wasm/JS boundary directly, so C++-side blend-state
    /// mapping (plan_pixijs.md Design decision 6) stays a pure, unit-testable function.
    enum class PixiJsBlendMode { Opaque = 0, AlphaBlend = 1, NonPremultiplied = 2, Additive = 3 };

    /** @brief Blend state shared by one PixiJS renderer and the SpriteBatch instances it creates. */
    struct PixiJsRendererState
    {
        /** @brief Blend mode captured by a SpriteBatch when it begins. */
        PixiJsBlendMode blendMode = PixiJsBlendMode::AlphaBlend;
    };

    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsRenderer::ApplyBlendState's
    /// own parameter doc) to a PixiJsBlendMode; throws std::runtime_error for any Blend/BlendFunction
    /// combination that isn't one of the 4 standard presets (plan_pixijs.md Design decision 6, v1
    /// scope -- PIXIJS-52 tracks a future fully-generic mapping). Contains no EM_JS/JS calls --
    /// exposed standalone so a future GTest run can unit test this mapping without a real
    /// PIXI.Application (plan_pixijs.md status block: nothing here has actually been run yet).
    PixiJsBlendMode BlendStateToPixiJsBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief PixiJS (pixijs.com) graphics renderer (Emscripten-only, 2D-only in v1 scope).
     *
     * See plan_pixijs.md for the full task breakdown and design rationale. As of this class's
     * initial authoring (Phase P1), the inherently-3D-only pure virtuals are wired to the shared
     * ThrowNo3D convention every 2D-only CNA renderer uses; the PixiJS-specific 2D draw path
     * (Phases P2-P6) is written but --  per plan_pixijs.md's own status block -- has not been run
     * against a real Emscripten toolchain or browser at all yet.
     */
    class PixiJsRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Constructs the renderer against an existing SDL window (Design decision 2).
         *
         * @param window Real SDL window already created by GraphicsDeviceManager; must not be null.
         * @param virtualWidth Initial virtual (game-logic) resolution width.
         * @param virtualHeight Initial virtual (game-logic) resolution height.
         * @param mode Presentation/scaling policy for virtual-vs-physical resolution.
         */
        PixiJsRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                       CnaPresentationMode mode);
        /** @brief Unregisters this renderer from the window registry. */
        ~PixiJsRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /** @brief Returns the real SDL window this renderer was constructed with. */
        SDL_Window* GetWindowInternal() const override { return window_; }
        /** @brief Always null -- no SDL_Renderer exists on this renderer (PixiJS owns its own WebGL context). */
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        /// plan_pixijs.md PIXIJS-35: a single PIXI.Application's default render pipeline targets one
        /// RenderTexture at a time in this renderer's v1 scope -- throws for count > 1, same
        /// conclusion CANVAS-26/HTML_DOM reached for their own single-target render paths.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /// plan_pixijs.md PIXIJS-50: maps the 4 standard BlendState presets to a PixiJsBlendMode
        /// (Design decision 6); throws for any other Blend/BlendFunction combination in this v1
        /// scope (PIXIJS-52 tracks a future fully-generic custom-blend-mode mapping).
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /// plan_pixijs.md PIXIJS-34: no PixiJS RenderTexture in this renderer's v1 scope carries a
        /// real depth/stencil attachment.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        /// plan_pixijs.md Design decision 6/12: PixiJS's built-in PIXI.BLEND_MODES.ADD is the
        /// renderer's real, tested mapping for BlendState::Additive -- everything 3D-only remains
        /// false in this v1, 2D-only scope.
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
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
        // Derives the logical (virtual) viewport size from the real canvas/window's physical pixel
        // size and virtualWidth_/virtualHeight_/presentationMode_ -- verbatim port of
        // CanvasRenderer::getLogicalSize (plan_pixijs.md Phase P2/PIXIJS-23): this math is
        // renderer-agnostic, only the underlying physical-size query is renderer-specific, and that
        // query is just SDL_GetWindowSize() here too.
        void getLogicalSize(int& width, int& height) const;

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        std::shared_ptr<PixiJsRendererState> state_ = std::make_shared<PixiJsRendererState>();
    };
}
