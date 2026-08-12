// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "HtmlDomState.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers::HtmlDom
{
    /**
     * @brief HTML DOM graphics renderer (Emscripten-only, 2D-only).
     *
     * Renders `SpriteBatch` output as real DOM elements styled with CSS -- one pooled `<div>` per
     * visible sprite, placed with a CSS `transform`, textured with `background-image`, faded with
     * `opacity` -- inside a `<div id="cna-dom-root">` this renderer creates over the `<canvas>`
     * SDL3's Emscripten video driver already owns. Nothing in the sprite path rasterizes into a
     * canvas: the browser's compositor does the drawing, so a sprite whose properties did not
     * change costs no CSS write at all (HTMLDOM-110 -- measured, not merely asserted: the JS flush
     * call itself still happens every frame, since a real XNA game keeps resubmitting static
     * sprites regardless), and a frame in which sprites only moved costs one `transform` write each.
     *
     * See `plan_html_dom.md` for the design decisions and `docs/html-dom-renderer.md` for the
     * capability boundary. The 3D surface -- depth/stencil clears, vertex/index buffers, every
     * `Draw*Primitives` entry point -- throws "not yet implemented"; `SupportsCapability()` lets a
     * caller check ahead of time instead of relying on the throw.
     */
    class HtmlDomRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the DOM surface over the given window's canvas element.
         *
         * @param window        The SDL window whose canvas element this renderer draws over.
         * @param virtualWidth  Logical (game-coordinate) width; 0 means "unset".
         * @param virtualHeight Logical (game-coordinate) height; 0 means "unset".
         * @param mode          Presentation/scaling policy.
         * @throws std::runtime_error if @p window is null.
         */
        HtmlDomRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                               CnaPresentationMode mode);

        /** @brief Removes the DOM surface and unregisters the renderer from its window. */
        ~HtmlDomRenderer() override;

        /**
         * @brief Clears the active target and drops every sprite queued this frame.
         *
         * plan_html_dom.md design decision 9: XNA's `Clear` overwrites everything drawn so far,
         * which in retained mode means resetting the element pool cursor to 0 and setting the root
         * container's `background-color`. With a render target bound it clears that canvas instead.
         *
         * @param r Red component, 0..1.
         * @param g Green component, 0..1.
         * @param b Blue component, 0..1.
         * @param a Alpha component, 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Ends the frame: hides pool elements the frame did not use and resizes the surface.
         *
         * There is nothing to swap or flush -- the browser compositor presents the DOM on its next
         * paint tick.
         */
        void Present() override;

        /**
         * @brief Returns the logical viewport size.
         *
         * @param width  Receives the logical width in game pixels.
         * @param height Receives the logical height in game pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Updates the logical presentation size at runtime.
         *
         * @param width  New logical width.
         * @param height New logical height.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Updates the presentation/scaling policy at runtime.
         *
         * plan_html_dom.md HTMLDOM-108: validates @p mode against `CnaPresentationMode`'s real
         * range -- an earlier version accepted any int and stored it unchecked, so an invalid
         * ordinal silently corrupted every later geometry computation instead of failing where the
         * bad value was actually passed in.
         *
         * @param mode Raw CnaPresentationMode ordinal.
         * @throws std::out_of_range if @p mode is not a valid CnaPresentationMode ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Converts physical window coordinates to logical game coordinates.
         *
         * plan_html_dom.md HTMLDOM-108: uses the same per-`CnaPresentationMode` `LogicalViewport`
         * geometry `Present()` renders with (`ComputeLogicalViewport()`), not a single
         * height-derived uniform scale that only happened to be correct for
         * `FixedHeightDynamicWidth`. Under `Letterbox`, a @p windowX/@p windowY that falls in the
         * letterbox bars (outside the scaled content rectangle) correctly returns false, matching
         * `SdlGpuRenderer::TransformWindowToLogical`'s own contract.
         *
         * @param windowX Physical X.
         * @param windowY Physical Y.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when a logical resolution is set and @p windowX/@p windowY falls within the
         *         scaled content rectangle; false otherwise (including every point in a Letterbox
         *         bar).
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts logical game coordinates to physical window coordinates.
         *
         * plan_html_dom.md HTMLDOM-108: exact inverse of `TransformWindowToLogical`, using the same
         * per-mode `LogicalViewport` geometry.
         *
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the physical X.
         * @param windowY Receives the physical Y.
         * @return True when a logical resolution is set; false otherwise.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /** @brief Always reports DepthFormat::None; no HTML DOM target owns depth/stencil storage. */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const override
        {
            return 0;
        }

        /**
         * @brief Clips subsequent rendering to the given rectangle, in logical game pixels.
         *
         * plan_html_dom.md HTMLDOM-80 / design decision 13: implemented as a real CSS
         * `clip-path: inset()`. HTMLDOM-102: only actually applied when
         * `RasterizerState.ScissorTestEnable` is true (see `ApplyRasterizerState`) -- an earlier
         * version applied it unconditionally, matching `SDL_RENDERER`'s own omission (which never
         * overrides `ApplyRasterizerState` at all) but not real XNA fidelity.
         *
         * This call itself only records the rectangle; it does not touch the DOM. The recorded
         * rectangle is consumed once per `SpriteBatch` Begin/End batch, when that batch's sprites
         * are flushed, and resolved to a dedicated clipped DOM container for that exact rectangle
         * (HTMLDOM-94) -- so a later call to this method with a different rectangle does not
         * retroactively reclip sprites an earlier batch already flushed under this one. Scoped
         * honestly at BATCH granularity, matching this renderer's one-flush-per-Begin/End
         * architecture: a `SpriteSortMode::Immediate` game that calls this between individual
         * `Draw()` calls inside one still-open `Begin()`/`End()` pair will not see finer-grained
         * clipping than "whatever this method last recorded when that batch's `End()` ran" -- the
         * same rule real XNA/FNA `SpriteBatch` itself applies to `GraphicsDevice` state read at
         * flush time in `Deferred` mode.
         *
         * The rectangle is remembered in logical coordinates and every active region's clip is
         * automatically re-derived against the surface's current logical size whenever that size
         * changes (e.g. a window resize with no `GraphicsDevice.Reset()`), so clips stay correct
         * without any caller needing to reissue them purely because the surface was resized
         * (HTMLDOM-93).
         *
         * @param x Left edge of the clip rectangle, in logical pixels.
         * @param y Top edge of the clip rectangle, in logical pixels.
         * @param w Width of the clip rectangle, in logical pixels.
         * @param h Height of the clip rectangle, in logical pixels.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Restricts rendering to a sub-rectangle of the current render target.
         *
         * plan_html_dom.md HTMLDOM-98. Confirmed non-gap against every other 2D-only sibling
         * (`SDL_RENDERER`/`CANVAS`/`DIRECTX3` all leave this as the inherited no-op too), but a real,
         * closeable one against `EASYGL`, which supports a genuine GL sub-region `Viewport`
         * (split-screen/sub-panel rendering) -- nothing about DOM/CSS compositing rules that out.
         *
         * plan_html_dom.md HTMLDOM-107: only RECORDS the viewport -- it does not touch the DOM at
         * all, the same shape `SetScissorRect` already uses. `#cna-dom-root` always stays at the
         * full backbuffer's own size/position; the viewport's own `(X,Y)` offset is instead
         * captured once per `SpriteBatch` flush and applied directly to that batch's own sprites
         * (both the DOM path and, while a `RenderTarget2D` is bound, the Canvas2D path). An earlier
         * version resized/repositioned root itself to the viewport sub-rect, which meant a SECOND
         * viewport set later in the same frame retroactively moved every sprite an EARLIER batch
         * had already flushed -- the real split-screen/sub-panel use case was not actually
         * implemented. Scissor and viewport are independent absolute-render-target-space concepts
         * on a real GPU (confirmed by reading `EasyGLRenderer`: its scissor forwards straight
         * to `glScissor`, unaffected by whatever `glViewport` region is active), so `SetScissorRect`'s
         * own region clip-path insets need no viewport-offset adjustment at all.
         *
         * `minDepth`/`maxDepth` are ignored -- this renderer has no depth buffer, matching how it
         * ignores `RasterizerState`'s own depth-bias fields.
         *
         * @param x Left edge of the viewport, in logical pixels.
         * @param y Top edge of the viewport, in logical pixels.
         * @param w Width of the viewport, in logical pixels.
         * @param h Height of the viewport, in logical pixels.
         * @param minDepth Ignored -- no depth buffer exists on this renderer.
         * @param maxDepth Ignored -- no depth buffer exists on this renderer.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Creates a texture from decoded image data.
         *
         * @param data Decoded RGBA8 image.
         * @return The new texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a sprite batch renderer.
         *
         * @return The new sprite batch renderer.
         */
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
         * @brief Creates an off-screen render target backed by a real canvas.
         *
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Must be DepthFormat::None.
         * @param preserveContents Honoured by the shared GraphicsDevice layer.
         * @param mipMap           Must be false; no mip chain exists on this renderer.
         * @param multiSampleCount Must be zero; no MSAA exists on this renderer.
         * @return The new render target renderer.
         * @throws std::runtime_error when an unsupported attachment, mip chain or MSAA is requested.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents = false,
                                                                   bool mipMap = false,
                                                                   int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief Binds a render target, or restores the DOM backbuffer.
         *
         * @param rt The target to bind, or null for the backbuffer.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a normalized render-target set.
         *
         * @param renderTargets The descriptors; null together with @p count 0 restores the backbuffer.
         * @param count         Number of descriptors.
         * @throws std::runtime_error for more than one target (there is no MRT concept here) or for
         *         a cube-face descriptor (this renderer has no cube targets).
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        /**
         * @brief Reads back rendered pixels from the currently bound render target.
         *
         * plan_html_dom.md design decision 11: no browser API rasterizes a live DOM subtree, so the
         * DOM backbuffer genuinely cannot be read back. A bound render target is a real canvas and
         * is read for real.
         *
         * @param x      Left edge of the region, in pixels.
         * @param y      Top edge of the region, in pixels.
         * @param w      Width of the region, in pixels.
         * @param h      Height of the region, in pixels.
         * @param pixels Destination for w * h * 4 RGBA8 bytes.
         * @throws std::runtime_error when no render target is bound.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Selects how subsequent draws prepare their source pixels and composite.
         *
         * @param colorSrcBlend  Raw Blend ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw Blend ordinal for the alpha source factor.
         * @param colorDstBlend  Raw Blend ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw Blend ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw BlendFunction ordinal for the colour channels.
         * @param alphaBlendFunc Raw BlendFunction ordinal for the alpha channel.
         * @param writeState     Colour write masks and coverage mask; both are inexpressible in CSS
         *                       compositing and are a documented capability gap, not a silent drop.
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
         * plan_html_dom.md HTMLDOM-102: `CullMode::None` and SpriteBatch's default
         * `CullCounterClockwiseFace` both preserve the renderer's ordinary sprite path;
         * `CullClockwiseFace` is rejected because this retained DOM renderer cannot cull by
         * post-transform winding;
         * `scissorTestEnable` is stored via `SetCurrentScissorEnableEXT` -- the same plain-C++,
         * per-batch-captured shape `ApplyBlendState` uses for `BlendState`. HTMLDOM-121 rejects
         * wireframe and non-zero depth bias because accepting them would claim state this CSS/DOM
         * renderer cannot represent.
         *
         * @param cullMode `None` or SpriteBatch's default `CullCounterClockwiseFace`.
         * @param fillMode Must be `FillMode::Solid`; wireframe is unsupported.
         * @param scissorTestEnable Whether `SetScissorRect`'s recorded rectangle should clip
         *                          subsequent draws.
         * @param depthBias Must be zero; no depth buffer exists on this renderer.
         * @param slopeScaleDepthBias Must be zero; no depth buffer exists on this renderer.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;

        /** @brief Always false -- neither the DOM backbuffer nor any target here has depth storage. */
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }

        /**
         * @brief Reports every graphics capability as unsupported, except
         *        `GraphicsCapability::AdditiveBlending`, which is queried for real.
         *
         * plan_html_dom.md HTMLDOM-117: `BlendState.Additive` degrades silently (not an exception)
         * on a browser without CSS `mix-blend-mode: plus-lighter` support -- this lets a caller
         * check for that ahead of time instead of only discovering it by comparing pixels.
         *
         * @param capability The capability being queried.
         * @return For `AdditiveBlending`, whether the running browser genuinely supports
         *         `mix-blend-mode: plus-lighter`; `false` for every other capability.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Rejects every shared public 3D draw route before it can consume renderer state. */
        void Ensure3DSupported(const char* operation) const override;

        /** @brief 3D: not yet implemented. @param r,g,b,a Clear colour. @param depth Depth value. */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /** @brief 3D: not yet implemented. @param depth Depth value. */
        void ClearDepth(float depth) override;
        /** @brief 3D: not yet implemented. @param stencil Stencil value. */
        void ClearStencil(int stencil) override;
        /** @brief 3D: not yet implemented. @param depth Depth value. @param stencil Stencil value. */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /** @brief 3D: not yet implemented. @param r,g,b,a Clear colour. @param stencil Stencil value. */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /** @brief 3D: not yet implemented. @param r,g,b,a Clear colour. @param depth Depth value. @param stencil Stencil value. */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        /** @brief 3D: not yet implemented. @param enabled Requested depth-test state. */
        void SetDepthTestEnabled(bool enabled) override;
        /** @brief 3D: not yet implemented. @param enabled Requested blend state. */
        void SetBlendEnabled(bool enabled) override;
        /** @brief 3D: not yet implemented. @param enabled Requested depth-write state. */
        void SetDepthWriteEnabled(bool enabled) override;

        /** @brief 3D: not yet implemented. @param vertex_capacity Requested capacity. @return Never returns. */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        /** @brief 3D: not yet implemented. @param index_capacity Requested capacity. @return Never returns. */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief 3D: not yet implemented.
         *
         * @param vb             Vertex source.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Primitive count.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief 3D: not yet implemented.
         *
         * @param vb             Vertex source.
         * @param ib             Index source.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Primitive count.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        /**
         * @brief The physical-pixel rectangle the logical (game-coordinate) surface occupies, and
         * the logical size itself.
         *
         * plan_html_dom.md HTMLDOM-108: the single computation every `CnaPresentationMode` value's
         * geometry derives from, ported from `SdlGpuRenderer::ComputeLogicalViewport` (the
         * "complete renderer" this task's own row names) rather than re-derived from scratch, so this
         * renderer's Letterbox/Overscan/Stretch/NativeBackBuffer math matches an already-tested
         * reference exactly instead of being its own independent (and, before this task, incomplete)
         * implementation. `x`/`y`/`width`/`height` are where the SCALED logical content actually
         * lands in physical pixels (Letterbox: `width`/`height` <= physical, centred, bars outside;
         * Overscan: `width`/`height` >= physical, centred, cropped at the physical edges;
         * Stretch/FixedHeightDynamicWidth/NativeBackBuffer: always the full physical rect, `x`=`y`=0).
         * `logicalWidth`/`logicalHeight` are the game-coordinate size `SpriteBatch`/`GetViewportSize`
         * work in -- NOT necessarily proportional to `width`/`height` (Stretch scales the two axes by
         * different factors on purpose).
         */
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        /// Computes this renderer's current LogicalViewport from the window's physical size,
        /// virtualWidth_/virtualHeight_, and presentationMode_. See LogicalViewport's own doc
        /// comment for the per-mode geometry this derives.
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;

        /// Derives the logical size from the window's physical size and the presentation mode --
        /// now a thin wrapper over ComputeLogicalViewport() (HTMLDOM-108), kept as its own method
        /// since GetViewportSize/SetVirtualResolution callers only ever need the size, not the full
        /// viewport rectangle.
        void getLogicalSize(int& width, int& height) const;

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        /// Last LogicalViewport pushed to JS; the DOM is only touched when this actually changes, so
        /// a steady-state frame performs no layout-affecting style writes at all. HTMLDOM-108:
        /// tracking the full viewport (not just logical/physical W/H, as before) so that a
        /// SetPresentationMode call alone -- same virtual and physical size, different mode, and
        /// therefore a different scale/offset -- is correctly detected as a geometry change too.
        int lastLogicalWidth_ = -1;
        int lastLogicalHeight_ = -1;
        int lastPhysicalWidth_ = -1;
        int lastPhysicalHeight_ = -1;
        float lastViewportOffsetX_ = 0.0f;
        float lastViewportOffsetY_ = 0.0f;
        float lastViewportScaleX_ = -1.0f;
        float lastViewportScaleY_ = -1.0f;
        /// Last viewport pushed to JS (HTMLDOM-98); SetViewport is a no-op when called again with
        /// the same values, matching the same "only touch the DOM when something actually changed"
        /// rule the logical/physical size tracking above already follows. -1 sentinel width/height
        /// guarantees the first real call is never skipped.
        int lastViewportX_ = 0;
        int lastViewportY_ = 0;
        int lastViewportWidth_ = -1;
        int lastViewportHeight_ = -1;
    };
}
