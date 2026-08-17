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
    /// `Custom` (PIXIJS-52) covers any Blend/BlendFunction combination outside the 4 standard
    /// presets -- its real GL blend factors/equations live on PixiJsRendererState's own
    /// `customBlend*` fields, populated by `XnaBlendToGlFactor`/`XnaBlendFunctionToGlEquation`.
    enum class PixiJsBlendMode { Opaque = 0, AlphaBlend = 1, NonPremultiplied = 2, Additive = 3, Custom = 4 };

    /** @brief Blend state shared by one PixiJS renderer and the SpriteBatch instances it creates. */
    struct PixiJsRendererState
    {
        /** @brief Blend mode captured by a SpriteBatch when it begins. */
        PixiJsBlendMode blendMode = PixiJsBlendMode::AlphaBlend;
        /// plan_pixijs.md PIXIJS-52: real WebGL blend-factor GL enum values, meaningful only when
        /// `blendMode == PixiJsBlendMode::Custom`. Applied to a reserved, mutated-in-place slot in
        /// PixiJS's own `renderer.state.blendModes` table at flush time (confirmed live, 2026-08-17,
        /// that this table accepts arbitrary custom entries and that PixiJS's own `setBlendMode`
        /// genuinely applies them via `gl.blendFuncSeparate`/`gl.blendEquationSeparate`).
        int customBlendSrcRGB = 1;
        int customBlendDstRGB = 0;
        int customBlendSrcAlpha = 1;
        int customBlendDstAlpha = 0;
        int customBlendEquationRGB = 32774;   // GL_FUNC_ADD
        int customBlendEquationAlpha = 32774; // GL_FUNC_ADD
    };

    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsRenderer::ApplyBlendState's
    /// own parameter doc) to a PixiJsBlendMode; returns `PixiJsBlendMode::Custom` for any
    /// Blend/BlendFunction combination that isn't one of the 4 standard presets (plan_pixijs.md
    /// PIXIJS-52 -- a real generic mapping, not a throw). Contains no EM_JS/JS calls -- exposed
    /// standalone so a native GTest run can unit test this mapping without a real PIXI.Application.
    PixiJsBlendMode BlendStateToPixiJsBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc);

    /// plan_pixijs.md PIXIJS-52: maps a raw XNA `Blend` enum value (0=One .. 12=SourceAlphaSaturation)
    /// to the real WebGL blend-factor GL enum value PixiJS's `renderer.state.blendModes` table
    /// expects (confirmed live against a real WebGL context, 2026-08-17). `BlendFactor`/
    /// `InverseBlendFactor` map to `CONSTANT_COLOR`/`ONE_MINUS_CONSTANT_COLOR` -- the actual RGBA
    /// constant itself (XNA's `GraphicsDevice.BlendFactor`) is not wired through in this v1 scope
    /// (WebGL's `gl.blendColor` is never set), so those two values are a real but partial mapping.
    int XnaBlendToGlFactor(int xnaBlend);

    /// plan_pixijs.md PIXIJS-52: maps a raw XNA `BlendFunction` enum value (0=Add .. 4=Min) to the
    /// real WebGL blend-equation GL enum value PixiJS's blend-mode table expects (confirmed live).
    int XnaBlendFunctionToGlEquation(int xnaBlendFunction);

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
         * @brief Constructs the renderer against the platform's presentation surface.
         *
         * The browser platform owns the `<canvas>` element; this renderer consumes only the
         * platform-neutral surface snapshot (window id, drawable size, display scale) and does
         * its drawing through PixiJS.
         *
         * @param args Construction arguments, already populated by GraphicsDevice.
         * @throws std::runtime_error If no platform window backs the surface.
         */
        explicit PixiJsRenderer(const GraphicsRendererCreateArgs& args);
        /** @brief Unregisters this renderer from the window registry. */
        ~PixiJsRenderer() override;

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
        /// plan_pixijs.md PIXIJS-35: a single PIXI.Application's default render pipeline targets one
        /// RenderTexture at a time in this renderer's v1 scope -- throws for count > 1, same
        /// conclusion CANVAS-26/HTML_DOM reached for their own single-target render paths.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /// plan_pixijs.md PIXIJS-50/52: maps the 4 standard BlendState presets to a PixiJsBlendMode
        /// (Design decision 6); any other Blend/BlendFunction combination gets a real, generic
        /// mapping via a custom PixiJS blend-mode table entry (PIXIJS-52), not a throw.
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
        // Derives the logical (virtual) viewport size from the canvas/window's client pixel size
        // and virtualWidth_/virtualHeight_/presentationMode_ -- the same FixedHeightDynamicWidth
        // math CanvasRenderer::getLogicalSize uses (plan_pixijs.md Phase P2/PIXIJS-23): the math is
        // renderer-agnostic, and the platform snapshot supplies the drawable size and density.
        void getLogicalSize(int& width, int& height) const;
        void getWindowSize(int& width, int& height) const;

        RendererSurfaceInfo surface_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        std::shared_ptr<PixiJsRendererState> state_ = std::make_shared<PixiJsRendererState>();
    };
}
