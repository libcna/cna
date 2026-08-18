#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers::PixiJs
{
    /// Classification of a `BlendState` against XNA's four standard presets. Kept as a dedicated
    /// enum (mirroring `CanvasCompositeOp`'s own reasoning) so the classification stays a pure,
    /// unit-testable C++ function with no EM_JS in it. `Custom` covers every other
    /// `Blend`/`BlendFunction` combination.
    ///
    /// PIXIJS-87: this classification no longer decides how a batch is *rendered*. Every blend
    /// state -- preset or not -- is rendered from its literal `XnaBlendToGlFactor`/
    /// `XnaBlendFunctionToGlEquation` factors through a dedicated `renderer.state.blendModes` slot.
    /// Mapping the presets onto PixiJS's own `PIXI.BLEND_MODES` values instead was the root cause
    /// of PIXIJS-51: PixiJS rewrites `NORMAL` to `NORMAL_NPM` (a different factor tuple) whenever
    /// the sampled texture is not premultiplied, so `BlendState::AlphaBlend` was silently rendered
    /// with `BlendState::NonPremultiplied`'s factors. The enum survives because it is a genuinely
    /// useful, tested description of a blend state, and because `Opaque` still selects PixiJS's
    /// real "no GL blending at all" path.
    enum class PixiJsBlendMode { Opaque = 0, AlphaBlend = 1, NonPremultiplied = 2, Additive = 3, Custom = 4 };

    /** @brief Graphics state shared by one PixiJS renderer and the SpriteBatch instances it creates. */
    struct PixiJsRendererState
    {
        /** @brief Blend classification captured by a SpriteBatch when it begins. */
        PixiJsBlendMode blendMode = PixiJsBlendMode::AlphaBlend;
        /// Literal WebGL blend factors/equations for the active `BlendState`, resolved by
        /// `XnaBlendToGlFactor`/`XnaBlendFunctionToGlEquation`. Populated for EVERY blend state,
        /// not only non-preset ones (PIXIJS-87).
        int blendSrcRGB = 1;
        int blendDstRGB = 0;
        int blendSrcAlpha = 1;
        int blendDstAlpha = 0;
        int blendEquationRGB = 32774;   // GL_FUNC_ADD
        int blendEquationAlpha = 32774; // GL_FUNC_ADD
        /// PIXIJS-88: `GraphicsDevice.BlendFactor` as WebGL's `gl.blendColor` constant, in 0..1.
        /// Only observable through the `BlendFactor`/`InverseBlendFactor` blend factors, but it is
        /// captured per batch so a later change cannot retroactively alter an already-submitted one.
        float blendFactorR = 0.0f;
        float blendFactorG = 0.0f;
        float blendFactorB = 0.0f;
        float blendFactorA = 0.0f;
        /// PIXIJS-89: `BlendState.ColorWriteChannels` for render-target slot 0, as the raw XNA int
        /// (bit0=R, 1=G, 2=B, 3=A; 15 = All). Applied through `gl.colorMask`.
        int colorWriteChannels = 15;
    };

    /// Pure classification of raw BlendState factors/BlendFunction (see
    /// IGraphicsRenderer::ApplyBlendState's own parameter doc) against XNA's four standard presets;
    /// returns `PixiJsBlendMode::Custom` for anything else. Contains no EM_JS/JS calls -- exposed
    /// standalone so a native GTest run can unit test this mapping without a real PIXI.Application.
    PixiJsBlendMode BlendStateToPixiJsBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc);

    /// Maps a raw XNA `Blend` enum value (0=One .. 12=SourceAlphaSaturation) to the real WebGL
    /// blend-factor GL enum value PixiJS's `renderer.state.blendModes` table expects (confirmed
    /// live against a real WebGL context). `BlendFactor`/`InverseBlendFactor` map to
    /// `CONSTANT_COLOR`/`ONE_MINUS_CONSTANT_COLOR`, whose RGBA constant is supplied for real by
    /// `PixiJsRenderer::SetBlendFactor` (PIXIJS-88).
    ///
    /// @throws std::runtime_error For a value outside the `Blend` enumeration.
    int XnaBlendToGlFactor(int xnaBlend);

    /// Maps a raw XNA `BlendFunction` enum value (0=Add .. 4=Min) to the real WebGL blend-equation
    /// GL enum value PixiJS's blend-mode table expects (confirmed live).
    ///
    /// @throws std::runtime_error For a value outside the `BlendFunction` enumeration.
    int XnaBlendFunctionToGlEquation(int xnaBlendFunction);

    /// PIXIJS-90: maps a raw XNA `TextureAddressMode` (0=Wrap, 1=Clamp, 2=Mirror) to the real
    /// `PIXI.WRAP_MODES` GL enum value (`gl.REPEAT`=10497, `gl.CLAMP_TO_EDGE`=33071,
    /// `gl.MIRRORED_REPEAT`=33648). Exposed for host testing.
    ///
    /// @throws std::runtime_error For a value outside the `TextureAddressMode` enumeration -- this
    ///         deliberately does not fall back to Clamp, which would silently render a state the
    ///         caller never asked for.
    int TextureAddressModeToPixiWrapMode(int addressMode);

    /// PIXIJS-90: true when a raw XNA `TextureFilter` magnifies linearly, which is the component a
    /// SpriteBatch draw actually observes (the same magnification-dominant grouping CANVAS-42
    /// established). Maps to `PIXI.SCALE_MODES.LINEAR` vs `.NEAREST`.
    ///
    /// @throws std::runtime_error For a value outside the `TextureFilter` enumeration.
    bool TextureFilterIsLinear(int textureFilter);

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
        /**
         * @brief Releases every renderer-owned JS resource.
         *
         * PIXIJS-92: destroys the scratch container and sprite pool, the reusable clear sprite,
         * every registered texture and render texture with their cached frame views, and removes
         * CNA's own state object from `Module`.
         *
         * The `PIXI.Application` is deliberately NOT destroyed: it is scoped to the platform's
         * `<canvas>`, not to this renderer. A canvas hands out exactly one WebGL context, and
         * PixiJS's own `Renderer.destroy()` loses it on purpose -- so tearing the application down
         * would leave the platform's canvas permanently unusable and make constructing a second
         * `GraphicsDevice` on the same page fail inside PixiJS's batch setup. Verified by doing
         * exactly that in a real browser. Every piece of state a later renderer could observe is
         * released here, so nothing stale carries across.
         */
        ~PixiJsRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        /**
         * @brief The platform surface snapshot this renderer is currently driving.
         *
         * PIXIJS-93: the drawable size here is the one PixiJS's own renderer has been sized to.
         * Exposed so a caller constructing a follow-up snapshot (a resize) can keep the window
         * identity the renderer was created with, rather than inventing one that
         * `OnSurfaceChanged` would rightly reject.
         *
         * @return The current surface snapshot.
         */
        [[nodiscard]] const RendererSurfaceInfo& GetSurfaceInfo() const { return surface_; }
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

        /**
         * @brief Applies a BlendState, honouring its write state or rejecting it explicitly.
         *
         * PIXIJS-87: every blend state is resolved to its literal WebGL factors/equations and
         * rendered through its own `renderer.state.blendModes` slot, so `AlphaBlend` and
         * `NonPremultiplied` are genuinely distinguished.
         *
         * @param writeState PIXIJS-89: `colorWriteChannels[0]` is honoured for real via
         *        `gl.colorMask`. Slots 1..3 describe MRT outputs this renderer never binds --
         *        `SetRenderTargets` rejects any count above one -- so they are inapplicable rather
         *        than dropped. `multiSampleMask` is accepted whenever sample 0 is enabled, which on
         *        this renderer's single-sample targets is exactly equivalent to the all-ones
         *        default; a mask that disables sample 0 is rejected.
         * @throws System::NotSupportedException If `multiSampleMask` disables sample 0.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Sets the constant blend colour used by the BlendFactor/InverseBlendFactor factors.
         *
         * PIXIJS-88: reaches WebGL's own `gl.blendColor`. Captured per batch at `Begin()`, so a
         * later change never retroactively alters an already-submitted draw.
         *
         * @param r Red component, 0..1.
         * @param g Green component, 0..1.
         * @param b Blue component, 0..1.
         * @param a Alpha component, 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

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
