// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

namespace CNA::Internal::Renderers::OpenVg
{
    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsRenderer::
    /// ApplyBlendState's own parameter doc) to a real ShivaVG VGBlendMode ordinal (VG_BLEND_SRC /
    /// VG_BLEND_SRC_OVER). Throws std::runtime_error for BlendState.Additive (ShivaVG declares
    /// VG_BLEND_ADDITIVE but never implements it) and for any other non-standard combination.
    /// Exposed standalone (contains no GL/OpenVG calls) so it can be unit tested directly -- see
    /// OpenVgRenderer.cpp for the full rationale.
    int BlendStateToVgBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                int colorDstBlend, int alphaDstBlend,
                                int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief OpenVG 1.1 vector-graphics renderer, implemented by ShivaVG on top of a real desktop
     * OpenGL context this renderer creates and owns itself (no EasyGL involved).
     *
     * See docs/openvg-renderer.md for the full capability boundary. In short: 2D-only (OpenVG has
     * no 3D pipeline at all -- every inherently-3D pure virtual throws by default, matching
     * Canvas/Skia's established pattern), real Clear/Present/textures/SpriteBatch through genuine
     * `vg*` OpenVG entry points, no render targets (ShivaVG has no EGL-VGImage-surface/FBO
     * equivalent to bind an off-screen VGImage as a draw target -- `CreateRenderTarget2D` keeps the
     * shared default `nullptr`, truthfully reporting "unsupported" rather than faking one).
     *
     * Presentation model: `ComputeLogicalViewportEXT()` is a direct port of the algorithm every
     * other CNA renderer with real Letterbox/Overscan/Stretch support uses (see e.g.
     * `OpenGL2Renderer::ComputeLogicalViewport`), adapted for ShivaVG's own GL_PROJECTION-based
     * pipeline: `GL_PROJECTION` is always an orthographic projection scoped to the CURRENT LOGICAL
     * size (`ComputeLogicalViewportEXT().logicalWidth/logicalHeight`), while `glViewport` places
     * that logical space onto the current PHYSICAL sub-rectangle -- the presentation-mode-derived
     * default rectangle (`GetDefaultViewportRect()`) until a caller explicitly overrides it via
     * `SetViewport()`, matching every sibling renderer's own "SetViewport is a raw pass-through"
     * convention. `SpriteBatch` draws (`OpenVgSpriteBatchRenderer`) compose their own per-draw
     * device-flip transform against the current LOGICAL height, not the physical framebuffer
     * height, so the SAME logical coordinate space, GL viewport transform and ortho combine to
     * place sprites correctly under every presentation mode -- not only `NativeBackBuffer`.
     *
     * Single-live-context invariant (see the .cpp's anonymous-namespace `ShivaVgOwnershipGuard`):
     * ShivaVG's own `vgCreateContextSH`/`vgDestroyContextSH` (`shContext.c`) operate on one
     * process-global `static VGContext *g_context` -- a second concurrently-live `OpenVgRenderer`
     * would otherwise silently alias the first renderer's context and then dangle once the first
     * is destroyed. Only one `OpenVgRenderer` may be constructed at a time in this process; a
     * second concurrent construction attempt throws immediately, and sequential
     * construct/destroy/construct cycles are fully supported.
     */
    class OpenVgRenderer final : public IGraphicsRenderer
    {
    public:
        explicit OpenVgRenderer(const GraphicsRendererCreateArgs& args);
        ~OpenVgRenderer() override;

        OpenVgRenderer(const OpenVgRenderer&) = delete;
        OpenVgRenderer& operator=(const OpenVgRenderer&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        /** @brief Refreshes the platform-neutral drawable snapshot after a window change. */
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

        // OpenVG's VG_BLEND_MODE only expresses a handful of fixed Porter-Duff-style modes (see
        // docs/openvg-renderer.md's blend table) -- maps the 4 standard XNA BlendState presets and
        // throws for any other Blend/BlendFunction combination, same shape as Canvas's
        // BlendStateToCompositeOp (CanvasRenderer.hpp).
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        // RasterizerState.ScissorTestEnable is the sole authority over whether OpenVG's
        // VG_SCISSORING is active (P0-5) -- SetScissorRect above only ever updates the rectangle.
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        // OpenVG has no depth or stencil buffer at all: benign/default DepthStencilState.None
        // (both enables false, matching SpriteBatch.Begin()'s own default) is silently accepted;
        // any meaningfully-enabled depth or stencil state is rejected rather than silently
        // dropped.
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass,
                                    int stencilFail, int stencilDepthFail, int stencilMask,
                                    int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail,
                                    int ccwStencilDepthFail) override;
        void SetReferenceStencil(int value) override;

        // OpenVG has no 3D pipeline at all -- the earliest possible guard for every modern
        // GraphicsDevice Draw*/DrawIndexed*/DrawInstanced* entry point (P0-6), run before any
        // vertex/index-buffer validation or sampler-state application.
        void Ensure3DSupported(const char* operation) const override;

        // OpenVG has no depth/stencil concept at all -- no render target, backbuffer included, has
        // one.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // OpenVG's VG_BLEND_ADDITIVE is declared by the spec but has no case in ShivaVG's own
            // updateBlendingStateGL (src/shPipeline.c) -- it silently falls through to normal
            // alpha blending. Reporting AdditiveBlending true would be exactly the "capability lie"
            // GraphicsCapability::AdditiveBlending's own doc comment warns against, so
            // ApplyBlendState throws for BlendState.Additive instead (see
            // BlendStateToVgBlendMode's own comment in OpenVgRenderer.cpp) and this reports false.
            //
            // Every other entry in CNA::GraphicsCapability is genuinely false here too: OpenVG is
            // 2D-only (ThreeD, DepthStencilBuffer, MultipleRenderTargets, StencilBuffer,
            // Instancing, MultiStreamVertexInput all false), never creates a multisample-capable
            // GL context (MultiSampleAntiAliasing false), never sets anisotropic filtering
            // (AnisotropicFiltering false), has no unfilled-polygon draw path (WireFrame false),
            // no real GPU query object (OcclusionQuery false), no programmable shader stage
            // (CustomEffects false), and no volume-texture storage of any kind (Texture3D false --
            // CreateTexture3D only ever returns a discard-everything null object under
            // WarnAndStub, matching Texture3DTests.cpp's own dual fixture).
            (void)capability;
            return false;
        }

        // ---- 3D: OpenVG is a 2D vector API with no 3D pipeline whatsoever. ----
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

        /// CNAEXT. One logical->physical presentation mapping, shared by GetViewportSize(),
        /// GetDefaultViewportRect(), the GL_PROJECTION ortho, TransformWindowToLogical/ToWindow
        /// and the scissor-rect mapping -- see the class doc comment. `x`/`y`/`width`/`height` are
        /// the PHYSICAL pixel sub-rectangle the current
        /// presentation mode maps the logical canvas onto; `logicalWidth`/`logicalHeight` are the
        /// logical canvas size itself. Mirrors OpenGL2Renderer::ComputeLogicalViewport's algorithm
        /// exactly, except physical-pixel-based throughout (not window-point-based) so it is
        /// correct under a high-DPI window scale factor -- see TransformWindowToLogical's own
        /// comment for why the window-point/physical-pixel distinction matters there too.
        struct LogicalViewport
        {
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            float logicalWidth = 0.0f, logicalHeight = 0.0f;
        };
        [[nodiscard]] LogicalViewport ComputeLogicalViewportEXT() const;

        /// CNAEXT. Re-syncs ShivaVG's own internal surface size (`vgResizeSurfaceSH`, which
        /// `vgClear`'s own clip-to-window clamp depends on -- see shContext.c) with the current
        /// physical window size, and reapplies this renderer's own ortho/viewport/scissor state on
        /// top when it does. A no-op when the physical size has not changed since the last call.
        /// Called from every entry point whose correctness depends on the physical surface size
        /// being current (Clear, SetViewport, SetScissorRect, SpriteBatch draws, readback) so a
        /// caller may legally resize the window and draw without ever calling Clear() first
        /// (P0-4).
        void EnsureSurfaceSizeEXT();

        /// CNAEXT. The current LOGICAL canvas height (`ComputeLogicalViewportEXT().logicalHeight`)
        /// -- what OpenVgSpriteBatchRenderer's own per-draw device-flip transform is scoped to
        /// (object space is logical, not physical; GL_PROJECTION's ortho and glViewport already
        /// handle the logical->physical presentation mapping -- see the class doc comment).
        [[nodiscard]] int GetLogicalHeightEXT() const;

        /// CNAEXT. Per-channel BlendWriteState.ColorWriteChannels[0] (R,G,B,A order) -- honored by
        /// OpenVgSpriteBatchRenderer via a real glColorMask wrapped tightly around each
        /// `vgDrawImage` call (verified safe: ShivaVG's own vgDrawImage only ever touches
        /// glColorMask on its VG_DRAW_IMAGE_MULTIPLY-with-a-non-color-paint branch, which this
        /// renderer's tint path -- always VG_PAINT_TYPE_COLOR -- never takes; see shPipeline.c).
        [[nodiscard]] const bool* GetColorWriteMaskEXT() const { return colorWriteMask_; }

    private:
        void applyOrtho();
        void applyViewportGL();
        void applyScissorRectGL();
        void applyScissorEnableGL();
        void refreshPresentationDerivedStateEXT();
        [[nodiscard]] int GetPhysicalHeightEXT() const;

        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        PlatformGlSurfaceState surface_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        bool blendEnabled_ = true;
        int lastBlendMode_ = 0x2001; // VG_BLEND_SRC_OVER (matches ShivaVG's own VGContext_ctor default)
        int swapInterval_ = 1;
        bool colorWriteMask_[4] = { true, true, true, true };

        int lastPhysW_ = 0, lastPhysH_ = 0;

        // Cached raw args exactly as last passed to SetViewport (physical pixels on
        // GraphicsDevice's own auto-reset path via GetDefaultViewportRect(); otherwise whatever
        // space the caller used -- see the class doc comment). Reapplied verbatim (only the Y-flip
        // is recomputed) whenever EnsureSurfaceSizeEXT() detects a physical resize, so an explicit
        // custom viewport survives Clear/resize (P0-3/P0-4) instead of being silently replaced.
        int viewportX_ = 0, viewportY_ = 0, viewportW_ = 0, viewportH_ = 0;
        float viewportMinDepth_ = 0.0f, viewportMaxDepth_ = 1.0f;
        bool viewportSet_ = false;

        // Cached LOGICAL scissor rect (RasterizerState-independent) + enable flag
        // (RasterizerState.ScissorTestEnable-driven, P0-5).
        int scissorX_ = 0, scissorY_ = 0, scissorW_ = 0, scissorH_ = 0;
        bool scissorEnabled_ = false;

        bool ownsGlobalContext_ = false;
        bool shivaContextCreated_ = false;
    };
}
