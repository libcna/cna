// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

struct NVGcontext;

namespace CNA::Internal::Renderers::NanoVg
{
    /**
     * @brief One `glBlendFuncSeparate` quadruple, expressed in `NVGblendFactor` values.
     *
     * NanoVG's own `NVGcompositeOperation` presets are a lossy vocabulary -- several distinct
     * `BlendState` configurations collapse onto the same preset -- so this renderer never uses
     * them. `nvgGlobalCompositeBlendFuncSeparate` takes exactly these four factors instead, which
     * is a 1:1 match for `BlendState`'s own colour/alpha source/destination pairs.
     */
    struct NanoVgBlendFunc
    {
        /** @brief `NVGblendFactor` applied to the source colour. */
        int srcRGB = 0;
        /** @brief `NVGblendFactor` applied to the destination colour. */
        int dstRGB = 0;
        /** @brief `NVGblendFactor` applied to the source alpha. */
        int srcAlpha = 0;
        /** @brief `NVGblendFactor` applied to the destination alpha. */
        int dstAlpha = 0;
    };

    /**
     * @brief Pure mapping from raw `BlendState` factor/function ordinals (see
     * `IGraphicsRenderer::ApplyBlendState`'s own parameter doc) to the four `NVGblendFactor`
     * values `nvgGlobalCompositeBlendFuncSeparate` takes.
     *
     * Every XNA `Blend` except `BlendFactor`/`InverseBlendFactor` has an exact `NVGblendFactor`
     * counterpart, so this is a direct per-factor translation rather than a preset match: the four
     * built-in `BlendState`s and every custom combination built from the representable factors are
     * all honoured exactly, with the colour and alpha channels independent.
     *
     * @param colorSrcBlend Raw `Blend` ordinal for the colour channels' source factor.
     * @param alphaSrcBlend Raw `Blend` ordinal for the alpha channel's source factor.
     * @param colorDstBlend Raw `Blend` ordinal for the colour channels' destination factor.
     * @param alphaDstBlend Raw `Blend` ordinal for the alpha channel's destination factor.
     * @param colorBlendFunc Raw `BlendFunction` ordinal for the colour channels.
     * @param alphaBlendFunc Raw `BlendFunction` ordinal for the alpha channel.
     * @return The equivalent NanoVG blend factors.
     * @throws std::runtime_error If any factor or function has no exact NanoVG/GL counterpart. The
     *         message names the offending property, because silently substituting a different
     *         blend is exactly the failure mode this renderer must not have.
     */
    NanoVgBlendFunc BlendStateToNvgBlendFunc(int colorSrcBlend, int alphaSrcBlend,
                                             int colorDstBlend, int alphaDstBlend,
                                             int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief NanoVG (memononen/nanovg, GL2 backend) vector-graphics renderer, on top of a real
     * desktop OpenGL context this renderer creates and owns itself (no EasyGL involved).
     *
     * See docs/nanovg-renderer.md for the full capability boundary and plan_nanovg.md for the
     * design decisions. In short: 2D-only (NanoVG has no 3D pipeline at all -- every inherently-3D
     * pure virtual throws by default, matching OpenVG/Canvas/Skia's established pattern), real
     * Clear/Present/textures/SpriteBatch through genuine `nvg*` NanoVG entry points, no render
     * targets (NanoVG's own off-screen-framebuffer helper, `nanovg_gl_utils.h`'s
     * `NVGLUframebuffer`, is deliberately out of this renderer's scope -- `CreateRenderTarget2D`
     * keeps the shared default `nullptr`).
     *
     * Unlike `OpenVgRenderer`, NanoVG's own coordinate system is already top-left-origin, Y-down
     * (matching HTML Canvas2D semantics) -- so no device-flip compensation is needed anywhere in
     * this renderer family. `SetScissorRect` stores its argument verbatim, in the render target's
     * own logical space; `NanoVgSpriteBatchRenderer` maps it into whichever space sprites are
     * currently addressed in and clips each quad against it geometrically (`nvgScissor` is
     * deliberately unused -- it is a fragment-shader mask, not a rasterizer clip; see that class's
     * own doc comment and docs/nanovg-renderer.md).
     *
     * Presentation model: `ComputeLogicalViewportEXT()` is the same algorithm every other CNA
     * renderer with real Letterbox/Overscan/Stretch support uses (ported directly from
     * `OpenVgRenderer::ComputeLogicalViewportEXT`), physical-pixel-based throughout. `glViewport`
     * places the current logical canvas onto the current physical sub-rectangle; each
     * `NanoVgSpriteBatchRenderer::Begin()` calls `nvgBeginFrame(ctx, logicalWidth, logicalHeight,
     * ratio)` scoped to the SAME logical size, so the two combine to map sprites correctly under
     * every presentation mode -- not only `NativeBackBuffer`.
     *
     * No single-live-context restriction: unlike ShivaVG (`shContext.c`'s process-global
     * `VGContext*`), NanoVG's `NVGcontext*` is an ordinary per-instance object with no hidden
     * global singleton, so multiple `NanoVgRenderer` instances may coexist in one process --
     * unlike a plain multi-window OpenGL app, the caller does not need to re-bind the current GL
     * context itself between instances: every entry point that issues GL/NanoVG calls (directly,
     * or indirectly through `NanoVgSpriteBatchRenderer`/`NanoVgTextureRenderer`, both of which
     * hold a reference back to their owning `NanoVgRenderer`) calls `MakeContextCurrentEXT()`
     * first, so instances may be freely interleaved from the same thread.
     */
    class NanoVgRenderer final : public IGraphicsRenderer
    {
    public:
        explicit NanoVgRenderer(const GraphicsRendererCreateArgs& args);
        ~NanoVgRenderer() override;

        NanoVgRenderer(const NanoVgRenderer&) = delete;
        NanoVgRenderer& operator=(const NanoVgRenderer&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        // NanoVG has no depth or stencil-facing buffer exposed to CNA (its own internal stencil
        // usage for stroke anti-aliasing is a private implementation detail, never a caller-
        // addressable DepthStencilState surface) -- DepthStencilState.None is accepted, matching
        // SpriteBatch.Begin()'s own default; anything meaningfully enabled is rejected.
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass,
                                    int stencilFail, int stencilDepthFail, int stencilMask,
                                    int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail,
                                    int ccwStencilDepthFail) override;
        void SetReferenceStencil(int value) override;

        void Ensure3DSupported(const char* operation) const override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        // ---- 3D: NanoVG is a 2D vector-graphics API with no 3D pipeline whatsoever. ----
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

        /// CNAEXT. One logical->physical presentation mapping, identical in shape to
        /// `OpenVgRenderer::LogicalViewport`/`ComputeLogicalViewportEXT` -- see that class's own
        /// doc comment.
        struct LogicalViewport
        {
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            float logicalWidth = 0.0f, logicalHeight = 0.0f;
        };
        [[nodiscard]] LogicalViewport ComputeLogicalViewportEXT() const;

        /// CNAEXT. The coordinate space `SpriteBatch` destination rectangles live in, i.e. what
        /// `nvgBeginFrame` must be handed, plus the mapping that carries a scissor rectangle into
        /// the same space.
        struct SpriteProjection
        {
            /** @brief Extent of the sprite coordinate space, for `nvgBeginFrame`. */
            float width = 0.0f, height = 0.0f;
            /** @brief `nvgBeginFrame`'s device-pixel ratio for that space. */
            float devicePixelRatio = 1.0f;
            /** @brief Maps a scissor rectangle from CNA's logical space into this space. */
            float scissorScaleX = 1.0f, scissorScaleY = 1.0f;
            float scissorOffsetX = 0.0f, scissorOffsetY = 0.0f;
            /** @brief True when a game-set `GraphicsDevice.Viewport` sub-region is active. */
            bool customViewport = false;
        };

        /**
         * @brief CNAEXT. Builds the current sprite coordinate space.
         *
         * XNA/FNA build the `SpriteBatch` projection from `Viewport.Width`/`Height`
         * (`CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height, 0, 0, 1)`), so a custom
         * `GraphicsDevice.Viewport` makes sprite destination rectangles VIEWPORT-LOCAL and the
         * rasterizer viewport alone positions the result -- `Viewport.X`/`Y` are never subtracted
         * from sprite coordinates. Handing `nvgBeginFrame` the full drawable while `glViewport`
         * holds a sub-region would instead squash every sprite into that sub-region.
         *
         * A custom viewport is one that differs from `GetDefaultViewportRect()`. Comparing against
         * that rather than against the whole drawable is what keeps the presentation modes working:
         * under `Letterbox`/`Overscan` the DEFAULT viewport is already a physical sub-rectangle, and
         * sprites there are still addressed in the logical (virtual-resolution) space, not in
         * physical pixels.
         */
        [[nodiscard]] SpriteProjection GetSpriteProjectionEXT() const;

        /// CNAEXT. Re-syncs the real `glViewport` with the current physical window size whenever
        /// it changed since the last call. Called from every entry point whose correctness
        /// depends on the physical surface size being current (Clear, SetViewport, SpriteBatch
        /// draws, readback) so a caller may resize the window and draw without ever calling
        /// Clear() first.
        void EnsureSurfaceSizeEXT();

        /// CNAEXT. The underlying `NVGcontext*`, for `NanoVgSpriteBatchRenderer`/
        /// `NanoVgTextureRenderer`.
        [[nodiscard]] NVGcontext* GetNvgContextEXT() const { return nvg_; }

        /// CNAEXT. Makes this renderer's own GL context current on the calling thread. OpenGL
        /// context state is global to the calling thread, not per-object -- with two or more live
        /// `NanoVgRenderer` instances (each owning its own context, see this class's own doc
        /// comment), whichever one's context was current LAST silently receives every subsequent
        /// GL call, including ones issued through a completely different instance. Every entry
        /// point that touches GL/NanoVG state calls this first so callers never have to manage
        /// context switching themselves.
        void MakeContextCurrentEXT() { platformContext_->MakeCurrent(); }

        /// CNAEXT. Current scissor rectangle + enable flag (RasterizerState.ScissorTestEnable-
        /// driven), in the render target's own logical space. `NanoVgSpriteBatchRenderer` carries
        /// it into the current sprite coordinate space (see `GetSpriteProjectionEXT`) and clips
        /// each quad against it geometrically.
        void GetScissorEXT(int& x, int& y, int& w, int& h, bool& enabled) const
        {
            x = scissorX_; y = scissorY_; w = scissorW_; h = scissorH_; enabled = scissorEnabled_;
        }

        /// CNAEXT. Current blend factors, applied by NanoVgSpriteBatchRenderer's own Begin() via
        /// `nvgGlobalCompositeBlendFuncSeparate`. With blending disabled these become plain source
        /// replacement -- NanoVG's own `glnvg__renderFlush` calls `glEnable(GL_BLEND)`
        /// unconditionally, so "no blending" has to be expressed as `(ONE, ZERO)` rather than by
        /// turning the blend stage off, which produces the identical result.
        [[nodiscard]] NanoVgBlendFunc GetBlendFuncEXT() const
        {
            if (blendEnabled_) return lastBlendFunc_;
            return NanoVgBlendFunc{/*NVG_ONE*/ 1 << 1, /*NVG_ZERO*/ 1 << 0,
                                   /*NVG_ONE*/ 1 << 1, /*NVG_ZERO*/ 1 << 0};
        }

    private:
        void applyViewportGL();
        void refreshPresentationDerivedStateEXT();
        [[nodiscard]] int GetPhysicalHeightEXT() const;

        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        PlatformGlSurfaceState surface_;
        NVGcontext* nvg_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        bool blendEnabled_ = true;
        /// BlendState.AlphaBlend's own factors (One, InverseSourceAlpha on both channels), which is
        /// what a GraphicsDevice starts with before any SpriteBatch.Begin() applies its own.
        NanoVgBlendFunc lastBlendFunc_{/*NVG_ONE*/ 1 << 1, /*NVG_ONE_MINUS_SRC_ALPHA*/ 1 << 7,
                                       /*NVG_ONE*/ 1 << 1, /*NVG_ONE_MINUS_SRC_ALPHA*/ 1 << 7};
        int swapInterval_ = 1;
        int lastPhysW_ = 0, lastPhysH_ = 0;

        int viewportX_ = 0, viewportY_ = 0, viewportW_ = 0, viewportH_ = 0;
        float viewportMinDepth_ = 0.0f, viewportMaxDepth_ = 1.0f;
        bool viewportSet_ = false;

        int scissorX_ = 0, scissorY_ = 0, scissorW_ = 0, scissorH_ = 0;
        bool scissorEnabled_ = false;
    };
}
