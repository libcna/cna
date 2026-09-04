// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"

#include "nanovg.h"

#include "System/NotSupportedException.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::NanoVg
{
    namespace
    {
        // nvgCreateImageRGBA/nvgUpdateImage both take tightly packed RGBA8 rows (no stride
        // parameter at all) -- repack whenever the caller's stride differs from width*4.
        const uint8_t* TightlyPack(const uint8_t* rgba, int width, int height, int strideBytes,
                                   std::vector<uint8_t>& scratch)
        {
            const int tightStride = width * 4;
            if (strideBytes == tightStride)
                return rgba;

            scratch.resize(static_cast<std::size_t>(tightStride) * static_cast<std::size_t>(height));
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(scratch.data() + static_cast<std::size_t>(row) * tightStride,
                           rgba + static_cast<std::size_t>(row) * strideBytes, tightStride);
            }
            return scratch.data();
        }
    }

    NanoVgTextureRenderer::NanoVgTextureRenderer(NanoVgRenderer& owner, const ImageData& data)
        : owner_(owner)
        , width_(data.width)
        , height_(data.height)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("NANOVG: texture width/height must be positive.");

        // nvgCreateImageRGBA allocates exactly one level and NanoVG's image API has no per-level
        // upload entry point at all, so a mip chain cannot be stored, uploaded into, or sampled
        // from here. Refusing at construction is the only honest answer: accepting the request
        // would leave Texture2D reporting LevelCount > 1 while every level above zero silently
        // vanished. Same conclusion, and the same construction-time gate, TinyGL reached for its
        // own single-level textures.
        // System::NotSupportedException rather than this renderer's usual std::runtime_error:
        // Texture2D's own public contract for an unsupported mip request is that type, which is
        // what the shared Texture2DTests.cpp arms assert (TINYGL reached the same conclusion and
        // the same guard).
        if (data.mipLevels != 1)
        {
            throw System::NotSupportedException(
                "NANOVG does not support mip-mapped Texture2D (requested " +
                std::to_string(data.mipLevels) + " levels): nvgCreateImageRGBA allocates a single "
                "level and NanoVG has no per-mip-level upload or LOD-sampling API. Create the "
                "texture with mipMap=false.");
        }

        // GL will not allocate beyond GL_MAX_TEXTURE_SIZE, and nvgCreateImageRGBA neither asks nor
        // checks -- an oversized glTexImage2D fails silently and leaves a texture object with no
        // storage, which samples as garbage instead of reporting anything. Refused here so the
        // failure is named at the call that caused it. This is a real device limit, distinct from
        // the GraphicsProfile ceiling Texture2D validates separately.
        const int maxEdge = owner_.GetMaxGlTextureSizeEXT();
        if (maxEdge > 0 && (width_ > maxEdge || height_ > maxEdge))
        {
            throw System::NotSupportedException(
                "NANOVG cannot create a " + std::to_string(width_) + "x" + std::to_string(height_) +
                " texture: this OpenGL implementation's GL_MAX_TEXTURE_SIZE is " +
                std::to_string(maxEdge) + ".");
        }

        const int stride = width_ * 4;
        if (!data.pixels.empty() &&
            static_cast<std::size_t>(stride) * static_cast<std::size_t>(height_) > data.pixels.size())
        {
            throw std::runtime_error("NANOVG: ImageData.pixels is smaller than width*height*4.");
        }

        std::vector<uint8_t> scratch;
        const uint8_t* initial = data.pixels.empty()
            ? nullptr
            : TightlyPack(data.pixels.data(), width_, height_, stride, scratch);

        // NVG_IMAGE_PREMULTIPLIED does NOT describe the bytes uploaded here -- ImageData is always
        // straight RGBA8, like every other CNA renderer's. It selects nanovg_gl.h's fragment-shader
        // branch: with the flag set, texType is 0 and the sampled texel is passed through
        // untouched; without it, texType is 1 and the shader runs
        // `color = vec4(color.xyz*color.w, color.w)` on every texel. XNA's own SpriteBatch pixel
        // shader performs no such multiply, so passing the texel through is what reproduces it --
        // and it is what lets BlendState.AlphaBlend receive genuinely premultiplied source RGB
        // without multiplying it a second time, and BlendState.Opaque write a translucent source's
        // full un-attenuated colour. The SourceAlpha factor NonPremultiplied/Additive need is then
        // applied by the real blend stage, where BlendState says it belongs.
        //
        // No sampler flags (NVG_IMAGE_NEAREST, NVG_IMAGE_REPEATX/Y) are set here either: those are
        // creation-time image properties, while XNA's SamplerState is chosen per
        // SpriteBatch.Begin(), independent of which texture is drawn. The batch writes the sampler
        // onto this image's GL texture object per draw instead -- see
        // ApplyNanoVgImageSamplerState's own doc comment.
        owner_.MakeContextCurrentEXT();
        image_ = nvgCreateImageRGBA(owner_.GetNvgContextEXT(), width_, height_,
                                    NVG_IMAGE_PREMULTIPLIED, initial);
        if (image_ == 0)
            throw std::runtime_error("NANOVG: nvgCreateImageRGBA failed.");
    }

    NanoVgTextureRenderer::~NanoVgTextureRenderer()
    {
        if (image_ != 0)
        {
            owner_.MakeContextCurrentEXT();
            nvgDeleteImage(owner_.GetNvgContextEXT(), image_);
        }
    }

    void NanoVgTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (image_ == 0 || !rgba) return;
        std::vector<uint8_t> scratch;
        const uint8_t* packed = TightlyPack(rgba, width_, height_, stride, scratch);
        owner_.MakeContextCurrentEXT();
        nvgUpdateImage(owner_.GetNvgContextEXT(), image_, packed);
    }

    void NanoVgTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                 int /*levelW*/, int /*levelH*/)
    {
        // The base-class default is an empty body, which would make Texture2D::SetData(level > 0)
        // succeed while discarding the upload -- exactly the silent approximation this renderer
        // must not have. Level 0 is a plain full-surface update; anything above it is refused,
        // matching every other CNA backend with no native mip chain (the SDL renderer family,
        // Canvas, DirectX1/2/8). Construction already refuses a mip-mapped Texture2D outright, so
        // reaching
        // a non-zero level here means the caller addressed a level this texture never had.
        if (level != 0)
        {
            throw System::NotSupportedException(
                "NANOVG does not support mip-level texture uploads (level " +
                std::to_string(level) + "): NanoVG images are single-level, with no per-level "
                "upload or LOD-sampling API. Use Texture2D::SetData(level=0, ...) only.");
        }
        UpdatePixels(rgba, width_ * 4);
    }
}
