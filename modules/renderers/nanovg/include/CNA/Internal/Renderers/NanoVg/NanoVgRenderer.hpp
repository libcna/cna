// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

struct NVGcontext;

namespace CNA::Internal::Renderers::NanoVg
{
    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsRenderer::
    /// ApplyBlendState's own parameter doc) to a real NanoVG `NVGcompositeOperation` ordinal.
    /// Unlike OpenVG (ShivaVG)'s `BlendStateToVgBlendMode`, this one genuinely supports Additive
    /// (`NVG_LIGHTER` is a real `(GL_ONE, GL_ONE)` `glBlendFuncSeparate` -- see
    /// `nanovg.c`'s own `nvg__compositeOperationState`). Exposed standalone (no GL/NanoVG calls)
    /// so it can be unit tested directly.
    int BlendStateToNvgCompositeOperation(int colorSrcBlend, int alphaSrcBlend,
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
     * this renderer family, and `nvgScissor`'s rectangle is expressed in the SAME logical
     * coordinate space `nvgRect`/`SpriteBatch` draws use (NanoVG maps it internally, the same way
     * it maps path geometry) -- unlike OpenVG's `VG_SCISSOR_RECTS`, which needed manual
     * presentation-mode remapping into physical pixels. `SetScissorRect` therefore stores its
     * argument verbatim.
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
     * global singleton, so multiple `NanoVgRenderer` instances may coexist in one process.
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

        /// CNAEXT. Re-syncs the real `glViewport` with the current physical window size whenever
        /// it changed since the last call. Called from every entry point whose correctness
        /// depends on the physical surface size being current (Clear, SetViewport, SpriteBatch
        /// draws, readback) so a caller may resize the window and draw without ever calling
        /// Clear() first.
        void EnsureSurfaceSizeEXT();

        /// CNAEXT. The underlying `NVGcontext*`, for `NanoVgSpriteBatchRenderer`/
        /// `NanoVgTextureRenderer`.
        [[nodiscard]] NVGcontext* GetNvgContextEXT() const { return nvg_; }

        /// CNAEXT. Current logical scissor rectangle + enable flag (RasterizerState.
        /// ScissorTestEnable-driven) -- applied by NanoVgSpriteBatchRenderer via `nvgScissor`,
        /// which (unlike OpenVG's `VG_SCISSOR_RECTS`) needs no presentation-mode remapping: it
        /// already operates in the same logical space `nvgRect` does.
        void GetScissorEXT(int& x, int& y, int& w, int& h, bool& enabled) const
        {
            x = scissorX_; y = scissorY_; w = scissorW_; h = scissorH_; enabled = scissorEnabled_;
        }

        /// CNAEXT. Current NanoVG composite operation ordinal (`NVGcompositeOperation`), applied
        /// by NanoVgSpriteBatchRenderer's own Begin() via `nvgGlobalCompositeOperation`.
        [[nodiscard]] int GetCompositeOperationEXT() const
        {
            return blendEnabled_ ? lastCompositeOp_ : /*NVG_COPY*/ 9;
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
        int lastCompositeOp_ = 0; // NVG_SOURCE_OVER
        int swapInterval_ = 1;
        int lastPhysW_ = 0, lastPhysH_ = 0;

        int viewportX_ = 0, viewportY_ = 0, viewportW_ = 0, viewportH_ = 0;
        float viewportMinDepth_ = 0.0f, viewportMaxDepth_ = 1.0f;
        bool viewportSet_ = false;

        int scissorX_ = 0, scissorY_ = 0, scissorW_ = 0, scissorH_ = 0;
        bool scissorEnabled_ = false;
    };
}
