// SPDX-License-Identifier: MS-PL
#pragma once

#include "SvgDomTextureRenderer.hpp"

namespace CNA::Internal::Renderers::SvgDom
{
    /**
     * @brief Off-screen render target backed by a real private `<canvas>` (plan_svg_dom.md design
     * decision 5).
     *
     * An `<svg>`/`<image>` element cannot be rendered into and read back synchronously (no browser
     * API rasterizes a live vector subtree to pixels without an async image decode), so -- exactly
     * like `CNA::Internal::Renderers::HtmlDom`'s own render targets -- a bound target switches the
     * sprite path over to a private Canvas2D context for the duration of the binding. The SVG
     * backbuffer path stays genuinely vector; only render-target-bound draws rasterize, and only
     * because real readback requires it.
     */
    class SvgDomRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        /**
         * @brief Creates an off-screen target of the given size, initially fully transparent.
         *
         * @param w Width in pixels.
         * @param h Height in pixels.
         */
        SvgDomRenderTargetRenderer(int w, int h);

        /** @brief Destroys the underlying canvas, if one was ever created. */
        ~SvgDomRenderTargetRenderer() override;

        /** @brief Returns the target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return texture_.GetWidth(); }

        /** @brief Returns the target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return texture_.GetHeight(); }

        /**
         * @brief Replaces the target's pixels directly, as for a plain texture.
         *
         * Authoritative until the next bind -- see `GetData`'s own `dirty_` handling.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /**
         * @brief Routes subsequent Clear/Draw calls into this target's own canvas.
         *
         * Marks the target dirty: its pixels can change through draws made while it is bound, which
         * never pass through UpdatePixels().
         */
        void BindAsRenderTarget() override;

        /** @brief Restores the SVG backbuffer as the active target. */
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127's contract: true only when the complete requested rectangle was written.
         * When the target has never been bound (or was last refreshed by `UpdatePixels`), this reads
         * straight from the CPU-side buffer both paths share -- works natively, no browser needed.
         * Once a bind has made the canvas the authoritative copy, a real `getImageData` readback
         * (Emscripten only) refreshes the CPU buffer first; a native (non-Emscripten) build has no
         * canvas to read from at that point and reports false rather than returning stale content.
         *
         * @return True once the whole rectangle has been written; false when no fresh copy exists.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Always false -- no target on this renderer has depth or stencil storage. */
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }

        /** @brief The only applied depth format is DepthFormat::None. */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(
            int /*requestedDepthStencilFormat*/) const override
        {
            return 0;
        }

        /** @brief CNAEXT. Returns this target's id into `Module['cnaSvgDomTextures']`. */
        [[nodiscard]] int GetCanvasIdEXT() const { return texture_.GetCanvasIdEXT(); }

        /**
         * @brief CNAEXT. Read-only access to the CPU-side RGBA8 buffer, refreshed from the canvas
         * first if a bind has left it stale (SVGDOM-D/E) -- so a render target drawn into and then
         * immediately used as a Draw() source (or the render-target-bound Canvas2D draw path's own
         * source pixel extraction) always sees current content, not a snapshot from before the most
         * recent bind.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& GetPixelsEXT() const
        {
            EnsureFreshEXT();
            return texture_.GetPixelsEXT();
        }

        /**
         * @brief CNAEXT. Data-URI accessor for sampling this render target as an ordinary Draw()
         * source texture (SVGDOM-E) -- refreshed from the canvas first if stale, for the same reason
         * as @ref GetPixelsEXT.
         */
        [[nodiscard]] const std::string& GetDataUriEXT(int variantMode) const
        {
            EnsureFreshEXT();
            return texture_.GetDataUriEXT(variantMode);
        }

    private:
        /**
         * @brief Refreshes the CPU-side buffer from the canvas if @ref dirty_, matching @ref
         * GetData's own readback contract. Extracted so GetData, GetPixelsEXT and GetDataUriEXT all
         * apply IDENTICAL staleness handling (SVGDOM-D/E) -- a caller can never observe canvas
         * content through one accessor and stale CPU-buffer content through another.
         */
        void EnsureFreshEXT() const;

        /// Mutable: GetData() is const (ITextureRenderer's own contract) but must be able to
        /// refresh this from a real getImageData readback the first time it is called after a bind.
        mutable SvgDomTextureRenderer texture_;
        /** @brief True once bound at least once since the last UpdatePixels/GetData refresh. */
        mutable bool dirty_ = false;
    };
}
