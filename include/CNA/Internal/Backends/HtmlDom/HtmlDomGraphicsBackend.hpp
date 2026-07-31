// SPDX-License-Identifier: MS-PL
#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "HtmlDomState.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Backends::HtmlDom
{
    /**
     * @brief HTML DOM graphics backend (Emscripten-only, 2D-only).
     *
     * Renders `SpriteBatch` output as real DOM elements styled with CSS -- one pooled `<div>` per
     * visible sprite, placed with a CSS `transform`, textured with `background-image`, faded with
     * `opacity` -- inside a `<div id="cna-dom-root">` this backend creates over the `<canvas>`
     * SDL3's Emscripten video driver already owns. Nothing in the sprite path rasterizes into a
     * canvas: the browser's compositor does the drawing, so a frame in which nothing moved costs
     * nothing at all, and a frame in which sprites only moved costs one `transform` write each.
     *
     * See `plan_html_dom.md` for the design decisions and `docs/html-dom-backend.md` for the
     * capability boundary. The 3D surface -- depth/stencil clears, vertex/index buffers, every
     * `Draw*Primitives` entry point -- throws "not yet implemented"; `SupportsCapability()` lets a
     * caller check ahead of time instead of relying on the throw.
     */
    class HtmlDomGraphicsBackend final : public IGraphicsBackend
    {
    public:
        /**
         * @brief Creates the DOM surface over the given window's canvas element.
         *
         * @param window        The SDL window whose canvas element this backend draws over.
         * @param virtualWidth  Logical (game-coordinate) width; 0 means "unset".
         * @param virtualHeight Logical (game-coordinate) height; 0 means "unset".
         * @param mode          Presentation/scaling policy.
         * @throws std::runtime_error if @p window is null.
         */
        HtmlDomGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                               CnaPresentationMode mode);

        /** @brief Removes the DOM surface and unregisters the backend from its window. */
        ~HtmlDomGraphicsBackend() override;

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
         * @param mode Raw CnaPresentationMode ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Converts physical window coordinates to logical game coordinates.
         *
         * @param windowX Physical X.
         * @param windowY Physical Y.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when a logical resolution is set; false otherwise.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts logical game coordinates to physical window coordinates.
         *
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the physical X.
         * @param windowY Receives the physical Y.
         * @return True when a logical resolution is set; false otherwise.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /** @brief Returns the SDL window this backend draws over. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }

        /** @brief Always null -- no `SDL_Renderer` exists on this backend, as on EASYGL. */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        /**
         * @brief Clips subsequent rendering to the given rectangle, in logical game pixels.
         *
         * plan_html_dom.md HTMLDOM-80 / design decision 13: implemented as a real CSS
         * `clip-path: inset()` on the DOM surface, applied unconditionally -- the same
         * `RasterizerState.ScissorTestEnable`-independent behaviour `SDL_RENDERER` has (it never
         * overrides `ApplyRasterizerState` either). Scoped as whole-surface, current-value
         * clipping: it clips everything currently on screen, not only sprites drawn while this
         * particular rectangle was active earlier in the same frame -- true per-draw-call
         * scissoring is out of scope for this backend's flat, pooled sprite architecture.
         *
         * The rectangle is remembered in logical coordinates and automatically re-applied against
         * the surface's current logical size whenever that size changes (e.g. a window resize with
         * no `GraphicsDevice.Reset()`), so the clip stays correct without the caller needing to
         * reissue it purely because the surface was resized (HTMLDOM-93).
         *
         * @param x Left edge of the clip rectangle, in logical pixels.
         * @param y Top edge of the clip rectangle, in logical pixels.
         * @param w Width of the clip rectangle, in logical pixels.
         * @param h Height of the clip rectangle, in logical pixels.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Creates a texture from decoded image data.
         *
         * @param data Decoded RGBA8 image.
         * @return The new texture backend.
         */
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a sprite batch backend.
         *
         * @return The new sprite batch backend.
         */
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /**
         * @brief Creates an off-screen render target backed by a real canvas.
         *
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Ignored -- no target here has depth storage.
         * @param preserveContents  Ignored -- handled entirely in the shared GraphicsDevice layer.
         * @param mipMap           Ignored -- no mip chain exists on this backend.
         * @param multiSampleCount Ignored -- no MSAA exists on this backend.
         * @return The new render target backend.
         */
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents = false,
                                                                   bool mipMap = false,
                                                                   int multiSampleCount = 0) override;

        /**
         * @brief Binds a render target, or restores the DOM backbuffer.
         *
         * @param rt The target to bind, or null for the backbuffer.
         */
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;

        /**
         * @brief Binds a normalized render-target set.
         *
         * @param renderTargets The descriptors; null together with @p count 0 restores the backbuffer.
         * @param count         Number of descriptors.
         * @throws std::runtime_error for more than one target (there is no MRT concept here) or for
         *         a cube-face descriptor (this backend has no cube targets).
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

        /** @brief Always false -- neither the DOM backbuffer nor any target here has depth storage. */
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }

        /**
         * @brief Reports every graphics capability as unsupported: this backend is 2D-only.
         *
         * @param capability The capability being queried.
         * @return False, for every currently enumerated capability.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability /*capability*/) const override
        {
            return false;
        }

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
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        /** @brief 3D: not yet implemented. @param index_capacity Requested capacity. @return Never returns. */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

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
        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
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
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        /// Derives the logical size from the window's physical size and the presentation mode --
        /// the same FixedHeightDynamicWidth math every other backend uses.
        void getLogicalSize(int& width, int& height) const;

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        /// Last surface geometry pushed to JS; the DOM is only touched when this actually changes,
        /// so a steady-state frame performs no layout-affecting style writes at all.
        int lastLogicalWidth_ = -1;
        int lastLogicalHeight_ = -1;
        int lastPhysicalWidth_ = -1;
        int lastPhysicalHeight_ = -1;
    };
}
