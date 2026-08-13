// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "SvgDomState.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::SvgDom
{
    /** @brief Copies pitched RGBA8 rows into the tightly packed representation this renderer keeps. */
    [[nodiscard]] std::vector<std::uint8_t> TightenTextureRowsEXT(
        const std::uint8_t* rgba, int width, int height, int stride);

    /**
     * @brief CNAEXT. Encodes tightly packed RGBA8 pixels as an in-memory PNG.
     *
     * Pure C++ (CNA's shared image encoder) -- no DOM/EM_JS access, so this and every
     * function below it are exercised directly by native GTest coverage without an Emscripten SDK,
     * unlike `HtmlDom`'s own PNG-encode-in-JS-canvas equivalent.
     *
     * @param rgba   Tightly packed RGBA8 rows, top row first.
     * @param width  Image width in pixels.
     * @param height Image height in pixels.
     * @return The encoded PNG bytes.
     * @throws std::runtime_error on an image-encoding failure.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodePngEXT(
        const std::uint8_t* rgba, int width, int height);

    /** @brief CNAEXT. Standard base64 encoding (RFC 4648, with padding). */
    [[nodiscard]] std::string Base64EncodeEXT(const std::uint8_t* data, std::size_t len);

    /** @brief CNAEXT. Wraps base64-encoded PNG bytes as a `data:image/png;base64,...` URI. */
    [[nodiscard]] std::string BuildPngDataUriEXT(const std::vector<std::uint8_t>& png);

    /**
     * @brief CNAEXT. Un-premultiplies straight-alpha... no -- divides RGB by alpha (the inverse of
     * premultiplication), producing the copy `BlendState.AlphaBlend` needs (see `DomCompositeOp`'s
     * own class doc for why).
     *
     * `rgb_out = clamp(round(rgb_in * 255 / a_in), 0, 255)` per channel when `a_in > 0`; pixels with
     * `a_in == 0` are copied unchanged (already `(0,0,0,0)` for any texture that was itself
     * premultiplied, and otherwise not visible regardless of their RGB).
     *
     * @param rgba   Tightly packed RGBA8 rows, top row first.
     * @param width  Image width in pixels.
     * @param height Image height in pixels.
     * @return The un-premultiplied copy, same size and layout.
     */
    [[nodiscard]] std::vector<std::uint8_t> UnpremultiplyEXT(
        const std::uint8_t* rgba, int width, int height);

    /**
     * @brief CNAEXT. Extracts a source sub-rectangle, applies the composite op's blend preparation
     * (un-premultiply for AlphaBlend, matching `DomCompositeOp`'s own contract) and multiplies every
     * channel by @p tint, forcing output alpha to 255 for `DomCompositeOp::Opaque`.
     *
     * Used only by the render-target-bound Canvas2D draw path (`SvgDomSpriteBatchRenderer::Flush`):
     * the SVG backbuffer path applies blend prep once per texture variant (cached) and tint via an
     * `feColorMatrix` filter at draw time instead, since baking tint into pixels per draw would
     * defeat the whole point of a compositor-driven, mostly-transform/opacity-only sprite path. Pure
     * C++ -- no DOM access -- so this is exercised directly by native GTest coverage.
     *
     * @param texturePixels Full tightly packed RGBA8 texture, top row first.
     * @param textureWidth  Full texture width in pixels.
     * @param sx,sy,sw,sh   Source sub-rectangle, in texture pixels; must lie within the texture.
     * @param tint          Tint colour; RGBA all multiply the texels (0..255 each channel).
     * @param op            The composite operation in effect.
     * @return @p sw * @p sh tightly packed RGBA8 pixels, top row first.
     */
    [[nodiscard]] std::vector<std::uint8_t> PrepareSpritePixelsEXT(
        const std::vector<std::uint8_t>& texturePixels, int textureWidth,
        int sx, int sy, int sw, int sh, const Color& tint, DomCompositeOp op);

    /**
     * @brief CNAEXT. Render-target-bound counterpart to @ref PrepareSpritePixelsEXT for a
     * Wrap/symmetric-Mirror source rectangle that leaves the texture (SVGDOM-1): samples every
     * output texel through wrap or mirror addressing instead of a single in-bounds copy, then
     * applies the same blend-prep/tint as @ref PrepareSpritePixelsEXT.
     *
     * Unlike the SVG backbuffer path (a `<pattern>` fill, tiling a pre-built texture variant), the
     * render-target path already rasterizes per draw, so it samples straight from the source
     * texture with no pre-tiled variant needed.
     *
     * @param texturePixels Full tightly packed RGBA8 texture, top row first.
     * @param textureWidth  Full texture width in pixels.
     * @param textureHeight Full texture height in pixels.
     * @param sx,sy,sw,sh   Source rectangle, in texture pixels; may leave the texture bounds.
     * @param mirrored      True for symmetric Mirror addressing, false for Wrap.
     * @param tint          Tint colour; RGBA all multiply the texels.
     * @param op            The composite operation in effect.
     * @return @p sw * @p sh tightly packed RGBA8 pixels, top row first.
     */
    [[nodiscard]] std::vector<std::uint8_t> PrepareTiledSpritePixelsEXT(
        const std::vector<std::uint8_t>& texturePixels, int textureWidth, int textureHeight,
        int sx, int sy, int sw, int sh, bool mirrored, const Color& tint, DomCompositeOp op);

    /**
     * @brief CNAEXT. Builds a 2*width x 2*height quadrant-mirrored tile from @p pixels, the source
     * image a symmetric `TextureAddressMode::Mirror` pattern fill tiles with plain `'repeat'` to
     * reproduce mirror-repeat addressing exactly (the same technique `HtmlDom` proved out for its
     * own equivalent `background-repeat` gap, independently applied here to an SVG/Canvas2D
     * `<pattern>`/`createPattern` fill instead of a CSS background).
     *
     * Quadrants: top-left is @p pixels unchanged; top-right is horizontally flipped; bottom-left is
     * vertically flipped; bottom-right is flipped on both axes. Tiling this 2x-sized image with a
     * plain (non-offset-corrected) repeat produces a seamless mirror-repeat at every tile boundary,
     * by construction.
     *
     * @param pixels Tightly packed RGBA8 rows, top row first.
     * @param width  Source image width in pixels.
     * @param height Source image height in pixels.
     * @return `(2*width) * (2*height)` tightly packed RGBA8 pixels, top row first.
     */
    [[nodiscard]] std::vector<std::uint8_t> BuildMirrorTiledVariantEXT(
        const std::vector<std::uint8_t>& pixels, int width, int height);

    /**
     * @brief Texture backed by a CPU-side RGBA8 buffer, with lazily generated/cached PNG data URIs.
     *
     * plan_svg_dom.md design decision 2. An SVG `<image>` element's `href` needs a URL; unlike
     * `HtmlDom` (which derives one from a private JS-side canvas via the async-free
     * `canvas.toDataURL()`), this renderer already owns the RGBA8 bytes in C++ (required for
     * `GetData`/`UpdatePixels` regardless) and encodes the PNG with CNA's shared image backend -- so the
     * data URI is available the instant it is asked for, with no JS canvas round-trip at all. The
     * straight (as-uploaded) and un-premultiplied variants are each encoded at most once per pixel
     * generation and cached until `UpdatePixels` invalidates them.
     */
    class SvgDomTextureRenderer : public ITextureRenderer
    {
    public:
        /**
         * @brief Creates a texture and stores @p data's pixels.
         *
         * @param data Decoded RGBA8 image, tightly packed, top row first.
         */
        explicit SvgDomTextureRenderer(const ImageData& data);

        /**
         * @brief Creates a blank (fully transparent) texture of the given size.
         *
         * Used by SvgDomRenderTargetRenderer, which has no initial pixels to upload.
         *
         * @param width  Width in pixels.
         * @param height Height in pixels.
         * @throws System::ArgumentOutOfRangeException if @p width or @p height is not positive.
         */
        SvgDomTextureRenderer(int width, int height);

        /** @brief Destroys the JS-side texture registry entry, if one was ever created. */
        ~SvgDomTextureRenderer() override;

        /** @brief Returns the texture width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the texture height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole level-0 image and invalidates every cached variant URI.
         *
         * @param rgba   Tightly packed RGBA8 rows, top row first; ignored when null.
         * @param stride Row pitch in bytes; padding is removed row by row when present.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /**
         * @brief Uploads a specific mip level.
         *
         * @throws std::runtime_error for any level above 0 -- no mip chain exists on this renderer,
         *         the same boundary the other browser 2D renderers draw.
         */
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief Reads back raw RGBA8 pixels from a sub-rectangle of level 0.
         *
         * Entirely CPU-side (this renderer always keeps the full RGBA8 buffer), so this works for
         * both an ordinary texture and a render target's own cached content -- unlike a renderer
         * whose only readback path is a native/browser API.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. Read-only access to the CPU-side RGBA8 buffer (tightly packed). */
        [[nodiscard]] const std::vector<std::uint8_t>& GetPixelsEXT() const { return pixels_; }

        /**
         * @brief CNAEXT. Returns the `data:image/png;base64,...` URI for the requested pixel
         * variant, encoding and caching it first if this is the first request since the last
         * `UpdatePixels`.
         *
         * @param variantMode 0 for the as-uploaded pixels, 1 for the un-premultiplied copy, 2 for
         *                    the mirror-tiled straight copy (`2*GetWidth() x 2*GetHeight()`), 3 for
         *                    the mirror-tiled un-premultiplied copy.
         * @return The data URI; never empty for a texture with a positive width/height.
         */
        [[nodiscard]] const std::string& GetDataUriEXT(int variantMode) const;

        /** @brief CNAEXT. Width in pixels of the image @ref GetDataUriEXT(variantMode) returns. */
        [[nodiscard]] int GetVariantWidthEXT(int variantMode) const
        {
            return (variantMode == 2 || variantMode == 3) ? width_ * 2 : width_;
        }

        /** @brief CNAEXT. Height in pixels of the image @ref GetDataUriEXT(variantMode) returns. */
        [[nodiscard]] int GetVariantHeightEXT(int variantMode) const
        {
            return (variantMode == 2 || variantMode == 3) ? height_ * 2 : height_;
        }

        /**
         * @brief CNAEXT. Returns this texture's id into `Module['cnaSvgDomTextures']`.
         *
         * Assigned lazily on first use by a renderer that pushes JS-side state (the sprite-batch
         * render-target path); 0 until then.
         */
        [[nodiscard]] int GetCanvasIdEXT() const { return id_; }

        /** @brief CNAEXT. Assigns (or re-registers) the JS-side canvas id for this texture. */
        void SetCanvasIdEXT(int id) { id_ = id; }

    protected:
        /** @brief Id into `Module['cnaSvgDomTextures']`; 0 until first registered. */
        int id_ = 0;
        /** @brief Texture width in pixels. */
        int width_ = 0;
        /** @brief Texture height in pixels. */
        int height_ = 0;
        /** @brief Tightly packed RGBA8 pixels, top row first. */
        std::vector<std::uint8_t> pixels_;

    private:
        mutable std::string variantUriCache_[4];
        mutable bool variantUriValid_[4] = {false, false, false, false};
    };
}
