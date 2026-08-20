// SPDX-License-Identifier: MS-PL
#pragma once

#include "HtmlDomTextureRenderer.hpp"

namespace CNA::Internal::Renderers::HtmlDom
{
    /**
     * @brief Off-screen render target backed by a real `<canvas>` (plans/plan_html_dom.md design
     * decision 10).
     *
     * A `<div>` cannot be rendered into, so the DOM sprite path has no way to express a render
     * target at all. Rather than reject `RenderTarget2D` -- which every 2D game using a post-effect
     * or a minimap needs -- a bound target switches the sprite path over to the target's own
     * Canvas2D context for the duration of the binding. When the target is later sampled as a
     * texture, its data URL is regenerated from the dirty flag every bind sets.
     */
    class HtmlDomRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        /**
         * @brief Creates an off-screen target of the given size, initially fully transparent.
         *
         * @param w Width in pixels.
         * @param h Height in pixels.
         */
        HtmlDomRenderTargetRenderer(int w, int h);

        /** @brief Destroys the underlying canvas and its cached variant URLs. */
        ~HtmlDomRenderTargetRenderer() override;

        /** @brief Returns the target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return texture_.GetWidth(); }

        /** @brief Returns the target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return texture_.GetHeight(); }

        /**
         * @brief Replaces the target's pixels directly, as for a plain texture.
         *
         * @param rgba   Tightly packed RGBA8 rows, top row first.
         * @param stride Row pitch in bytes.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override { texture_.UpdatePixels(rgba, stride); }

        /**
         * @brief Routes subsequent Clear/Draw/readback calls into this target's own canvas.
         *
         * Also marks the target's cached data URLs stale: its pixels can change through draws made
         * while it is bound, which never pass through UpdatePixels().
         */
        void BindAsRenderTarget() override;

        /**
         * @brief Restores the DOM backbuffer as the active target.
         *
         * Unlike a GL renderer's unbind there is no MSAA resolve or mip regeneration to perform --
         * this renderer has neither.
         */
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127's contract: true only when the complete requested rectangle was written.
         * A render target here is a real off-screen canvas, so `getImageData` reads it synchronously
         * and exactly; a native (non-Emscripten) build has no canvas at all and reports false rather
         * than letting the shared layer hand the caller its own zeroed scratch buffer.
         *
         * plans/plan_html_dom.md HTMLDOM-106: the returned bytes are always straight (non-premultiplied)
         * alpha, regardless of which BlendState drew into this target -- `getImageData`'s own
         * contract, not a choice this renderer makes. This is a deliberate deviation from real XNA,
         * where a render target's raw storage reflects whatever representation was actually drawn
         * into it (e.g. premultiplied, after an AlphaBlend draw). Sampling this target back as a
         * `Texture2D` under `BlendState.AlphaBlend` accounts for this (see `cnaDomGetVariant`'s own
         * comment in HtmlDomTextureRenderer.cpp) rather than assuming premultiplied input.
         *
         * @param level      Mip level; only 0 exists on this renderer.
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false when no canvas exists.
         * @throws System::NotSupportedException if @p level is above 0.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the target, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Always false -- no target on this renderer has depth or stencil storage.
         *
         * @param depthFormatWasRequested What the public RenderTarget2D asked for; ignored, since
         *        honouring it would mean claiming storage that does not exist.
         * @return False, always.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }

        /** @brief The only applied depth format is DepthFormat::None. */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(
            int /*requestedDepthStencilFormat*/) const override
        {
            return 0;
        }

        /**
         * @brief CNAEXT. Returns this target's id into `Module['cnaDomTextures']`.
         *
         * @return The JS-side canvas id backing this target.
         */
        [[nodiscard]] int GetCanvasId() const { return texture_.GetCanvasId(); }

    private:
        HtmlDomTextureRenderer texture_;
    };
}
