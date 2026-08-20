// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::HtmlDom
{
    /** @brief Copies pitched RGBA8 rows into the tightly packed representation browser APIs need. */
    [[nodiscard]] std::vector<std::uint8_t> TightenTextureRowsEXT(
        const std::uint8_t* rgba, int width, int height, int stride);

    /**
     * @brief Texture backed by a private off-screen canvas plus a cache of CSS-usable data URLs.
     *
     * plans/plan_html_dom.md design decision 6. CSS `background-image` needs a URL, and the only
     * synchronous canvas-to-URL route a browser offers is `canvas.toDataURL()` -- `toBlob`/
     * `convertToBlob` are asynchronous and would let draws reorder. Each instance therefore owns
     * one JS-side canvas registered under a unique integer id in `Module['cnaDomTextures']`, and
     * the PNG data URLs derived from it are generated lazily and cached (per blend variant and
     * tint colour, HTMLDOM-23) until the pixels change.
     *
     * The practical consequence is stated plainly in `docs/html-dom-renderer.md`: uploading a
     * texture costs a PNG encode, which is fine once at load time and expensive every frame.
     */
    class HtmlDomTextureRenderer : public ITextureRenderer
    {
    public:
        /**
         * @brief Creates a texture and uploads @p data's pixels into its off-screen canvas.
         *
         * @param data Decoded RGBA8 image, tightly packed, top row first.
         */
        explicit HtmlDomTextureRenderer(const ImageData& data);

        /**
         * @brief Creates a blank (fully transparent) texture of the given size.
         *
         * Used by HtmlDomRenderTargetRenderer, which has no initial pixels to upload.
         *
         * @param width  Width in pixels.
         * @param height Height in pixels.
         */
        HtmlDomTextureRenderer(int width, int height);

        /** @brief Destroys the JS-side canvas and every cached variant URL derived from it. */
        ~HtmlDomTextureRenderer() override;

        /** @brief Returns the texture width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the texture height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole level-0 image and invalidates every cached variant URL.
         *
         * @param rgba   Tightly packed RGBA8 rows, top row first; ignored when null.
         * @param stride Row pitch in bytes; padding is removed row by row when present.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /**
         * @brief Uploads a specific mip level.
         *
         * @param rgba   Tightly packed RGBA8 rows for that level, top row first.
         * @param level  Mip level; only 0 exists on this renderer.
         * @param levelW Width at that level, in pixels.
         * @param levelH Height at that level, in pixels.
         * @throws std::runtime_error for any level above 0 -- neither CSS backgrounds nor this
         *         renderer's texture canvases have a mip chain or per-level LOD selection, the same
         *         boundary `CANVAS` and the native 2D renderer draw.
         */
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief CNAEXT. Returns this texture's id into `Module['cnaDomTextures']`.
         *
         * Used by HtmlDomSpriteBatchRenderer to name the source texture in a draw command, and by
         * HtmlDomRenderTargetRenderer for its own bind/readback.
         *
         * @return The JS-side canvas id; never 0 for a live texture.
         */
        [[nodiscard]] int GetCanvasId() const { return id_; }

    protected:
        /** @brief Id into `Module['cnaDomTextures']`, unique per live texture. */
        int id_ = 0;
        /** @brief Texture width in pixels. */
        int width_ = 0;
        /** @brief Texture height in pixels. */
        int height_ = 0;
    };
}
