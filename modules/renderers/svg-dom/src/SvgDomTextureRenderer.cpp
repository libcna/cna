// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"

#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_svg_dom.md design decision 2: registers (or updates) a texture's data-URI variant in the
// JS-side registry the SVG backbuffer flush path (SvgDomSpriteBatchRenderer.cpp) resolves
// <image href> from. Idempotent per (id, variantMode) from the C++ side (only called once per
// variant generation -- see SvgDomTextureRenderer::GetDataUriEXT). `texW`/`texH` are the texture's
// own natural size (always the same across every call for a given id); `variantW`/`variantH` are
// this specific variant's own image size -- equal to texW/texH for variants 0/1, but 2x for the
// mirror-tiled variants 2/3 (SVGDOM-1), so a <pattern> tile sized from the wrong dimensions never
// happens even though a single texture can have differently-sized cached variants.
EM_JS(void, CNA_SvgDom_RegisterTextureVariant, (int id, int variantMode, const char* uri,
                                                int texW, int texH, int variantW, int variantH), {
    if (typeof document === 'undefined') return;
    if (!Module['cnaSvgDomTextures']) Module['cnaSvgDomTextures'] = {};
    let entry = Module['cnaSvgDomTextures'][id];
    if (!entry) { entry = { w: texW, h: texH }; Module['cnaSvgDomTextures'][id] = entry; }
    // SVGDOM-E: additive, not "only when the entry doesn't exist yet". A render target's own entry
    // (created by CNA_SvgDom_CreateTargetCanvas: { canvas, ctx, w, h, isRenderTarget: true }) already
    // exists by the time a RenderTarget2D is first sampled as an ordinary Draw() source -- the OLD
    // code's `if (!entry) {...}` skipped initializing variants/variantDims for that pre-existing
    // shape entirely, so the very next line (`entry.variants[variantMode] = ...`) threw
    // "Cannot set properties of undefined" the instant any game did SpriteBatch.Draw(renderTarget).
    // One entry now coherently supports BOTH roles at once: a render target IS-A texture (matching
    // IRenderTargetRenderer : ITextureRenderer), so its registry entry can carry canvas/ctx AND
    // variants/variantDims simultaneously -- ordinary textures never gain canvas/ctx (their id is
    // never passed to CNA_SvgDom_CreateTargetCanvas), so this never runs the other direction.
    if (!entry.variants) entry.variants = {};
    if (!entry.variantDims) entry.variantDims = {};
    entry.variants[variantMode] = UTF8ToString(uri);
    entry.variantDims[variantMode] = { w: variantW, h: variantH };
    entry.w = texW; entry.h = texH;
});

// SVGDOM (texture lifecycle): the counterpart to CNA_SvgDom_RegisterTextureVariant -- removes an
// ordinary texture's JS registry entry (including its cached base64 PNG data-URI strings) when the
// owning SvgDomTextureRenderer is destroyed. Without this, every destroyed Texture2D leaked its
// registry entry forever (ids are never reused -- AllocateTextureIdEXT is a monotonically
// increasing counter -- so this was a pure, unbounded leak across a game's lifetime, not a
// use-after-free risk, but a real one on top of `delete` on a missing key being a harmless no-op
// for a texture that was constructed but never actually drawn (GetDataUriEXT lazily creates the
// entry on first use, so plenty of short-lived textures never had one to begin with).
EM_JS(void, CNA_SvgDom_DestroyTexture, (int id), {
    const reg = Module['cnaSvgDomTextures'];
    if (reg) delete reg[id];
});
#endif

namespace CNA::Internal::Renderers::SvgDom
{
    std::vector<std::uint8_t> TightenTextureRowsEXT(
        const std::uint8_t* rgba, int width, int height, int stride)
    {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4);
        const int rowBytes = width * 4;
        for (int y = 0; y < height; ++y)
            std::memcpy(out.data() + static_cast<std::size_t>(y) * rowBytes,
                       rgba + static_cast<std::size_t>(y) * stride, rowBytes);
        return out;
    }

    std::vector<std::uint8_t> EncodePngEXT(const std::uint8_t* rgba, int width, int height)
    {
        return CNA::Internal::Graphics::ImageLoader::EncodePng(
            rgba, width, height, width, height);
    }

    std::string Base64EncodeEXT(const std::uint8_t* data, std::size_t len)
    {
        static const char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);

        std::size_t i = 0;
        for (; i + 3 <= len; i += 3)
        {
            const std::uint32_t n =
                (static_cast<std::uint32_t>(data[i]) << 16) |
                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                static_cast<std::uint32_t>(data[i + 2]);
            out.push_back(kTable[(n >> 18) & 0x3F]);
            out.push_back(kTable[(n >> 12) & 0x3F]);
            out.push_back(kTable[(n >> 6) & 0x3F]);
            out.push_back(kTable[n & 0x3F]);
        }

        const std::size_t remaining = len - i;
        if (remaining == 1)
        {
            const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
            out.push_back(kTable[(n >> 18) & 0x3F]);
            out.push_back(kTable[(n >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        }
        else if (remaining == 2)
        {
            const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
            out.push_back(kTable[(n >> 18) & 0x3F]);
            out.push_back(kTable[(n >> 12) & 0x3F]);
            out.push_back(kTable[(n >> 6) & 0x3F]);
            out.push_back('=');
        }
        return out;
    }

    std::string BuildPngDataUriEXT(const std::vector<std::uint8_t>& png)
    {
        return "data:image/png;base64," + Base64EncodeEXT(png.data(), png.size());
    }

    std::vector<std::uint8_t> UnpremultiplyEXT(const std::uint8_t* rgba, int width, int height)
    {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
        for (std::size_t p = 0; p < pixelCount; ++p)
        {
            const std::uint8_t r = rgba[p * 4 + 0];
            const std::uint8_t g = rgba[p * 4 + 1];
            const std::uint8_t b = rgba[p * 4 + 2];
            const std::uint8_t a = rgba[p * 4 + 3];
            if (a == 0)
            {
                out[p * 4 + 0] = 0;
                out[p * 4 + 1] = 0;
                out[p * 4 + 2] = 0;
                out[p * 4 + 3] = 0;
                continue;
            }
            const float invA = 255.0f / static_cast<float>(a);
            out[p * 4 + 0] = static_cast<std::uint8_t>(
                std::clamp(std::lround(r * invA), 0L, 255L));
            out[p * 4 + 1] = static_cast<std::uint8_t>(
                std::clamp(std::lround(g * invA), 0L, 255L));
            out[p * 4 + 2] = static_cast<std::uint8_t>(
                std::clamp(std::lround(b * invA), 0L, 255L));
            out[p * 4 + 3] = a;
        }
        return out;
    }

    namespace
    {
        void ValidateSize(int width, int height)
        {
            if (width <= 0 || height <= 0)
                throw System::ArgumentOutOfRangeException(
                    "SVG_DOM renderer: texture width/height must be positive (got " +
                    std::to_string(width) + "x" + std::to_string(height) + ").");
            // Overflow-safe upper bound: width_*height_*4 (the tightly packed RGBA8 buffer size)
            // must stay representable both as a native std::size_t (32-bit on Emscripten/wasm32 --
            // the platform this renderer actually ships on, unlike a 64-bit native test host) and as
            // the plain `int` this renderer's own GetData/UpdatePixels/EncodePngEXT surface uses
            // throughout (dataLength comparisons, PNG row strides, ...). Computed in int64_t so the
            // check itself cannot overflow before it has a chance to reject an oversized request.
            const std::int64_t byteCount =
                static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) * 4;
            if (byteCount > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()))
                throw System::ArgumentOutOfRangeException(
                    "SVG_DOM renderer: texture " + std::to_string(width) + "x" + std::to_string(height) +
                    " (" + std::to_string(byteCount) + " RGBA8 bytes) exceeds what this renderer can "
                    "represent -- its buffer sizes and lengths are plain 32-bit int throughout.");
        }
    }

    SvgDomTextureRenderer::SvgDomTextureRenderer(const ImageData& data)
        : id_(AllocateTextureIdEXT()), width_(data.width), height_(data.height)
    {
        ValidateSize(width_, height_);
        pixels_.assign(data.pixels.begin(), data.pixels.end());
        pixels_.resize(static_cast<std::size_t>(width_) * height_ * 4, 0);
    }

    SvgDomTextureRenderer::SvgDomTextureRenderer(int width, int height)
        : id_(AllocateTextureIdEXT()), width_(width), height_(height)
    {
        ValidateSize(width_, height_);
        pixels_.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);
    }

    SvgDomTextureRenderer::~SvgDomTextureRenderer()
    {
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_DestroyTexture(id_);
#endif
    }

    void SvgDomTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!rgba) return;
        pixels_ = TightenTextureRowsEXT(rgba, width_, height_, stride);
        for (bool& valid : variantUriValid_) valid = false;
    }

    void SvgDomTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw std::runtime_error(
                "SVG_DOM renderer: no mip chain exists on this renderer; only level 0 can be updated.");
        UpdatePixels(rgba, levelW * 4);
    }

    bool SvgDomTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                        void* data, int dataLength) const
    {
        if (level != 0 || !data) return false;
        if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
        // Overflow-safe (see ValidateSize's own comment): x/y/w/h/dataLength are caller-controlled
        // ints reaching this from the public Texture2D::GetData surface. Plain `int` arithmetic for
        // x+w, y+h or w*h*4 can wrap for inputs near INT_MAX, which would let an out-of-bounds
        // region slip past these checks and into the per-row memcpy below.
        if (static_cast<std::int64_t>(x) + w > width_ || static_cast<std::int64_t>(y) + h > height_)
            return false;
        if (static_cast<std::int64_t>(dataLength) < static_cast<std::int64_t>(w) * h * 4) return false;

        auto* out = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const std::uint8_t* src = pixels_.data() +
                (static_cast<std::size_t>(y + row) * width_ + x) * 4;
            std::memcpy(out + static_cast<std::size_t>(row) * w * 4, src, static_cast<std::size_t>(w) * 4);
        }
        return true;
    }

    const std::string& SvgDomTextureRenderer::GetDataUriEXT(int variantMode) const
    {
        const int idx = (variantMode >= 0 && variantMode <= 3) ? variantMode : 0;
        if (!variantUriValid_[idx])
        {
            std::vector<std::uint8_t> png;
            switch (idx)
            {
                case 1:
                    png = EncodePngEXT(
                        UnpremultiplyEXT(pixels_.data(), width_, height_).data(), width_, height_);
                    break;
                case 2:
                {
                    const std::vector<std::uint8_t> tiled =
                        BuildMirrorTiledVariantEXT(pixels_, width_, height_);
                    png = EncodePngEXT(tiled.data(), width_ * 2, height_ * 2);
                    break;
                }
                case 3:
                {
                    const std::vector<std::uint8_t> unpremul =
                        UnpremultiplyEXT(pixels_.data(), width_, height_);
                    const std::vector<std::uint8_t> tiled =
                        BuildMirrorTiledVariantEXT(unpremul, width_, height_);
                    png = EncodePngEXT(tiled.data(), width_ * 2, height_ * 2);
                    break;
                }
                default:
                    png = EncodePngEXT(pixels_.data(), width_, height_);
                    break;
            }
            variantUriCache_[idx] = BuildPngDataUriEXT(png);
            variantUriValid_[idx] = true;
#if defined(__EMSCRIPTEN__)
            CNA_SvgDom_RegisterTextureVariant(
                id_, idx, variantUriCache_[idx].c_str(), width_, height_,
                GetVariantWidthEXT(idx), GetVariantHeightEXT(idx));
#endif
        }
        return variantUriCache_[idx];
    }

    std::vector<std::uint8_t> BuildMirrorTiledVariantEXT(
        const std::vector<std::uint8_t>& pixels, int width, int height)
    {
        const int outW = width * 2;
        const int outH = height * 2;
        std::vector<std::uint8_t> out(static_cast<std::size_t>(outW) * outH * 4);

        auto srcPixel = [&](int x, int y) -> const std::uint8_t*
        {
            return pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
        };
        auto dstPixel = [&](int x, int y) -> std::uint8_t*
        {
            return out.data() + (static_cast<std::size_t>(y) * outW + x) * 4;
        };

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t* src = srcPixel(x, y);
                std::memcpy(dstPixel(x, y), src, 4);                             // top-left
                std::memcpy(dstPixel(outW - 1 - x, y), src, 4);                  // top-right (flip X)
                std::memcpy(dstPixel(x, outH - 1 - y), src, 4);                  // bottom-left (flip Y)
                std::memcpy(dstPixel(outW - 1 - x, outH - 1 - y), src, 4);       // bottom-right (flip both)
            }
        }
        return out;
    }

    namespace
    {
        // Shared by PrepareSpritePixelsEXT and PrepareTiledSpritePixelsEXT: both extract an sw x sh
        // buffer of texels (by straight in-bounds copy or by wrap/mirror sampling, respectively)
        // and then apply the identical blend-prep/tint step below.
        void ApplyBlendPrepAndTintEXT(std::vector<std::uint8_t>& pixels, int w, int h,
                                      const Color& tint, DomCompositeOp op)
        {
            if (op == DomCompositeOp::AlphaBlend)
                pixels = UnpremultiplyEXT(pixels.data(), w, h);

            const int tintR = tint.getRProperty();
            const int tintG = tint.getGProperty();
            const int tintB = tint.getBProperty();
            const int tintA = tint.getAProperty();
            const std::size_t pixelCount = static_cast<std::size_t>(w) * h;
            for (std::size_t p = 0; p < pixelCount; ++p)
            {
                pixels[p * 4 + 0] = static_cast<std::uint8_t>((pixels[p * 4 + 0] * tintR) / 255);
                pixels[p * 4 + 1] = static_cast<std::uint8_t>((pixels[p * 4 + 1] * tintG) / 255);
                pixels[p * 4 + 2] = static_cast<std::uint8_t>((pixels[p * 4 + 2] * tintB) / 255);
                pixels[p * 4 + 3] = (op == DomCompositeOp::Opaque)
                    ? static_cast<std::uint8_t>(255)
                    : static_cast<std::uint8_t>((pixels[p * 4 + 3] * tintA) / 255);
            }
        }

        // Non-negative modulo -- an out-of-bounds source origin can be negative (a sourceRectangle
        // scrolled past the left/top edge under Wrap/Mirror addressing).
        int ProperModEXT(int v, int m)
        {
            const int r = v % m;
            return r < 0 ? r + m : r;
        }

        // Folds v into [0, w) via the standard hardware mirror-repeat function: periodic with
        // period 2w, identity on [0, w) and reflected on [w, 2w) -- matches
        // BuildMirrorTiledVariantEXT's own quadrant layout (see that function's doc).
        int MirrorFoldEXT(int v, int w)
        {
            const int period = 2 * w;
            const int m = ProperModEXT(v, period);
            return m < w ? m : (period - 1 - m);
        }
    }

    std::vector<std::uint8_t> PrepareSpritePixelsEXT(
        const std::vector<std::uint8_t>& texturePixels, int textureWidth,
        int sx, int sy, int sw, int sh, const Color& tint, DomCompositeOp op)
    {
        std::vector<std::uint8_t> sub(static_cast<std::size_t>(sw) * sh * 4);
        for (int row = 0; row < sh; ++row)
        {
            const std::uint8_t* src = texturePixels.data() +
                (static_cast<std::size_t>(sy + row) * textureWidth + sx) * 4;
            std::memcpy(sub.data() + static_cast<std::size_t>(row) * sw * 4, src,
                       static_cast<std::size_t>(sw) * 4);
        }
        ApplyBlendPrepAndTintEXT(sub, sw, sh, tint, op);
        return sub;
    }

    std::vector<std::uint8_t> PrepareTiledSpritePixelsEXT(
        const std::vector<std::uint8_t>& texturePixels, int textureWidth, int textureHeight,
        int sx, int sy, int sw, int sh, bool mirrored, const Color& tint, DomCompositeOp op)
    {
        std::vector<std::uint8_t> sub(static_cast<std::size_t>(sw) * sh * 4);
        for (int row = 0; row < sh; ++row)
        {
            const int gy = sy + row;
            const int texY = mirrored ? MirrorFoldEXT(gy, textureHeight) : ProperModEXT(gy, textureHeight);
            for (int col = 0; col < sw; ++col)
            {
                const int gx = sx + col;
                const int texX = mirrored ? MirrorFoldEXT(gx, textureWidth) : ProperModEXT(gx, textureWidth);
                const std::uint8_t* src = texturePixels.data() +
                    (static_cast<std::size_t>(texY) * textureWidth + texX) * 4;
                std::memcpy(sub.data() + (static_cast<std::size_t>(row) * sw + col) * 4, src, 4);
            }
        }
        ApplyBlendPrepAndTintEXT(sub, sw, sh, tint, op);
        return sub;
    }
}
