// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "SvgDomState.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers::SvgDom
{
    /**
     * @brief SVG DOM graphics renderer (Emscripten-only, 2D-only).
     *
     * Renders `SpriteBatch` output as real SVG DOM elements -- one `<svg>` viewport (its own
     * `viewBox` crops the source rectangle) containing one `<image>` per visible sprite, placed
     * with a collapsed affine `transform="matrix(...)"`, tinted with a cached `feColorMatrix`
     * filter -- inside an `<svg id="cna-svg-dom-root">` this renderer creates over the `<canvas>`
     * SDL3's Emscripten video driver already owns. This is a genuinely different draw route from
     * both `CNA::Internal::Renderers::Canvas` (rasterizes into a `<canvas>` 2D context) and
     * `CNA::Internal::Renderers::HtmlDom` (CSS-transformed `<div>`s with `background-image`): every
     * sprite is real vector content composited by the browser's own SVG renderer, and per-sprite
     * tint is a native SVG filter primitive rather than a pre-tinted cached bitmap variant.
     *
     * Sprite elements are pooled and reused across frames with per-attribute dirty diffing (see
     * `SvgDomSpriteBatchRenderer`) -- a steady frame costs no DOM writes beyond the pool's initial
     * creation. See `docs/svg-dom-renderer.md` for the capability boundary and the remaining V1
     * scope decisions (a single global scissor region rather than per-batch-isolated regions,
     * in-bounds-only source rectangles) that make this a real but smaller-scope sibling of the more
     * mature `HtmlDom` renderer. The 3D surface -- depth/stencil clears, vertex/index buffers,
     * every `Draw*Primitives` entry point -- throws "not yet implemented"; `SupportsCapability()`
     * lets a caller check ahead of time.
     */
    class SvgDomRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the SVG surface over the given window's canvas element.
         *
         * @param window        The SDL window whose canvas element this renderer draws over.
         * @param virtualWidth  Logical (game-coordinate) width; 0 means "unset".
         * @param virtualHeight Logical (game-coordinate) height; 0 means "unset".
         * @param mode          Presentation/scaling policy.
         * @throws std::runtime_error if @p window is null.
         */
        SvgDomRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                      CnaPresentationMode mode);

        /** @brief Removes the SVG surface and unregisters the renderer from its window. */
        ~SvgDomRenderer() override;

        /**
         * @brief Clears the active target and rewinds the pooled sprite cursor for this frame.
         *
         * With no render target bound, sets the backbuffer's own background colour and rewinds the
         * pooled-sprite cursor to 0 (the once-per-frame reset every SpriteBatch flush this frame
         * appends onto) -- existing pool elements are reused/dirty-diffed, not destroyed. With a
         * render target bound, clears that target's own canvas instead.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Ends the frame: hides only the pooled sprite elements this frame did not use.
         *
         * There is nothing to swap -- the browser's own SVG renderer presents on its next paint
         * tick, the same as `HtmlDom`'s DOM/CSS surface.
         */
        void Present() override;

        /** @brief Returns the logical viewport size. */
        void GetViewportSize(int& width, int& height) override;

        /** @brief Updates the logical presentation size at runtime. */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Updates the presentation/scaling policy at runtime.
         *
         * @throws std::out_of_range if @p mode is not a valid CnaPresentationMode ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Converts physical window coordinates to logical game coordinates.
         *
         * @return True when a logical resolution is set and the point falls within the scaled
         *         content rectangle; false otherwise (including a Letterbox bar).
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /** @brief Exact inverse of TransformWindowToLogical. */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /** @brief Always reports DepthFormat::None; no SVG DOM target owns depth/stencil storage. */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const override
        {
            return 0;
        }

        /**
         * @brief Clips subsequent rendering to the given rectangle, in logical game pixels.
         *
         * Recorded only when `RasterizerState.ScissorTestEnable` is true (see
         * `ApplyRasterizerState`); realized as a real SVG `<clipPath>` per flush (SVGDOM-A), and as a
         * real Canvas2D `clip()` bracket on the render-target-bound path (SVGDOM-B). Intersected with
         * the active `Viewport` rectangle (SVGDOM-F) -- see
         * `SvgDomState::ComputeEffectiveClipRectEXT`. Each `SpriteBatch` flush claims its own
         * document-ordered slot, so cross-flush paint order always matches actual draw order
         * regardless of how scissor rects interleave (see docs/svg-dom-renderer.md).
         *
         * @param x Left edge of the clip rectangle, in logical pixels.
         * @param y Top edge of the clip rectangle, in logical pixels.
         * @param w Width of the clip rectangle, in logical pixels.
         * @param h Height of the clip rectangle, in logical pixels.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Records the active Viewport rectangle for subsequent sprite draws (SVGDOM-F).
         *
         * Recorded only -- does not touch the DOM. The (X,Y) offset is composed as the outermost
         * translation on each sprite's own matrix at `SpriteBatch` flush time
         * (SvgDomSpriteBatchRenderer::QueueDraw), matching real XNA/FNA's rasterizer-stage
         * `Viewport.X/Y` application, strictly after `SpriteBatch.Begin(transformMatrix)`.
         * Width/Height define an unconditional clip rectangle (independent of
         * `RasterizerState.ScissorTestEnable`), intersected with any active scissor rect -- see
         * `SvgDomState::ComputeEffectiveClipRectEXT`.
         *
         * `minDepth`/`maxDepth` are ignored -- this renderer has no depth buffer.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /** @brief Creates a texture from decoded image data. */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /** @brief Creates a sprite batch renderer. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /** @brief Unsupported resource factories reject directly instead of returning null. */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(
            const std::string& vertSrc, const std::string& fragSrc) override;

        /**
         * @brief Creates an off-screen render target backed by a real private canvas.
         *
         * @throws std::runtime_error when an unsupported attachment, mip chain or MSAA is requested.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents = false,
                                                                   bool mipMap = false,
                                                                   int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /** @brief Binds a render target, or restores the SVG backbuffer. */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a normalized render-target set.
         *
         * @throws std::runtime_error for more than one target or a cube-face descriptor.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        /**
         * @brief Reads back rendered pixels from the currently bound render target.
         *
         * No browser API rasterizes a live SVG subtree synchronously, so the SVG backbuffer itself
         * genuinely cannot be read back -- the same boundary `HtmlDom` draws for its own DOM/CSS
         * surface. A bound render target is a real canvas and is read for real.
         *
         * @throws std::runtime_error when no render target is bound.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Selects how subsequent draws prepare their source pixels and composite.
         *
         * @throws std::runtime_error for any BlendState outside the four standard presets.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /** @brief Accepts only the fully disabled depth/stencil state used by 2D SpriteBatch. */
        void ApplyDepthStencilState(
            bool depthEnable, bool depthWriteEnable, int depthFunc,
            bool stencilEnable, int stencilFunc, int stencilPass, int stencilFail,
            int stencilDepthFail, int stencilMask, int stencilWriteMask, int referenceStencil,
            bool twoSidedStencilMode, int ccwStencilFunc, int ccwStencilPass,
            int ccwStencilFail, int ccwStencilDepthFail) override;

        /** @brief Zero is inert; a non-zero stencil reference is unsupported and rejected. */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Records whether subsequent draws should honour the scissor rect.
         *
         * @param cullMode `None` or SpriteBatch's default `CullCounterClockwiseFace`.
         * @param fillMode Must be `FillMode::Solid`; wireframe is unsupported.
         * @param scissorTestEnable Whether `SetScissorRect`'s recorded rectangle should clip.
         * @param depthBias Must be zero; no depth buffer exists on this renderer.
         * @param slopeScaleDepthBias Must be zero; no depth buffer exists on this renderer.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;

        /** @brief Always false -- neither the SVG backbuffer nor any target here has depth storage. */
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }

        /**
         * @brief Reports every graphics capability as unsupported, except
         *        `GraphicsCapability::AdditiveBlending`, which is queried for real.
         *
         * @return For `AdditiveBlending`, whether the running browser genuinely supports
         *         `mix-blend-mode: plus-lighter`; `false` for every other capability.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Rejects every shared public 3D draw route before it can consume renderer state. */
        void Ensure3DSupported(const char* operation) const override;

        /** @brief 3D: not yet implemented. */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /** @brief 3D: not yet implemented. */
        void ClearDepth(float depth) override;
        /** @brief 3D: not yet implemented. */
        void ClearStencil(int stencil) override;
        /** @brief 3D: not yet implemented. */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /** @brief 3D: not yet implemented. */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /** @brief 3D: not yet implemented. */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        /** @brief 3D: not yet implemented. */
        void SetDepthTestEnabled(bool enabled) override;
        /** @brief 3D: not yet implemented. */
        void SetBlendEnabled(bool enabled) override;
        /** @brief 3D: not yet implemented. */
        void SetDepthWriteEnabled(bool enabled) override;

        /** @brief 3D: not yet implemented. */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        /** @brief 3D: not yet implemented. */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /** @brief 3D: not yet implemented. */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;

        /** @brief 3D: not yet implemented. */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        /// See HtmlDomRenderer::LogicalViewport's own doc -- the identical XNA
        /// CnaPresentationMode contract, independently computed here.
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        void getLogicalSize(int& width, int& height) const;
        void ApplySurfaceGeometryEXT();

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
    };
}
