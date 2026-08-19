// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"

#include "nanovg.h"

#include <cstring>
#include <stdexcept>
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
}
