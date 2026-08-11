// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomTextureRenderer.hpp"

#include "System/ArgumentOutOfRangeException.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
    if (!entry) { entry = { variants: {}, variantDims: {}, w: texW, h: texH }; Module['cnaSvgDomTextures'][id] = entry; }
    entry.variants[variantMode] = UTF8ToString(uri);
    entry.variantDims[variantMode] = { w: variantW, h: variantH };
    entry.w = texW; entry.h = texH;
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
        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(rgba), width * 4);
        if (!surface)
            throw std::runtime_error(std::string("SVG_DOM: SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        SDL_IOStream* dst = SDL_IOFromDynamicMem();
        if (!dst)
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("SVG_DOM: SDL_IOFromDynamicMem failed: ") + SDL_GetError());
        }

        if (!IMG_SavePNG_IO(surface, dst, false))
        {
            SDL_CloseIO(dst);
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("SVG_DOM: IMG_SavePNG_IO failed: ") + SDL_GetError());
        }

        std::vector<std::uint8_t> png;
        const Sint64 size = SDL_TellIO(dst);
        if (size > 0)
        {
            auto* buf = static_cast<std::uint8_t*>(
                SDL_GetPointerProperty(SDL_GetIOProperties(dst),
                                       SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
            if (buf)
                png.assign(buf, buf + size);
        }

        SDL_CloseIO(dst);
        SDL_DestroySurface(surface);
        return png;
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

    SvgDomTextureRenderer::~SvgDomTextureRenderer() = default;

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
        if (x + w > width_ || y + h > height_) return false;
        if (dataLength < w * h * 4) return false;

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
