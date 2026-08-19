// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgGlLoader.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "nanovg.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::NanoVg
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        /// Whether a raw `TextureFilter` ordinal minifies, respectively magnifies, with point
        /// sampling. Real desktop GL stores `GL_TEXTURE_MIN_FILTER` and `GL_TEXTURE_MAG_FILTER`
        /// independently, so every XNA filter whose two components differ is representable exactly.
        /// The mip component of the six mip-qualified ordinals is inert here and named as such in
        /// docs/nanovg-renderer.md: `nvgCreateImageRGBA` allocates exactly one level, so there is
        /// no chain to select between.
        void DecodeTextureFilter(int textureFilterOrdinal, bool& minifyPoint, bool& magnifyPoint)
        {
            switch (textureFilterOrdinal)
            {
            case 0: minifyPoint = false; magnifyPoint = false; return; // Linear
            case 1: minifyPoint = true;  magnifyPoint = true;  return; // Point
            case 3: minifyPoint = false; magnifyPoint = false; return; // LinearMipPoint
            case 4: minifyPoint = true;  magnifyPoint = true;  return; // PointMipLinear
            case 5:                                                    // MinLinearMagPointMipLinear
            case 6: minifyPoint = false; magnifyPoint = true;  return; // MinLinearMagPointMipPoint
            case 7:                                                    // MinPointMagLinearMipLinear
            case 8: minifyPoint = true;  magnifyPoint = false; return; // MinPointMagLinearMipPoint
            case 2:
                throw std::runtime_error(
                    "NANOVG does not support TextureFilter.Anisotropic: no anisotropic sampler is "
                    "configured on this renderer, which is why "
                    "SupportsCapability(AnisotropicFiltering) reports false.");
            default:
                throw std::runtime_error(
                    "NANOVG: unsupported TextureFilter ordinal " +
                    std::to_string(textureFilterOrdinal) + ".");
            }
        }

        void ValidateAddressMode(int textureAddressModeOrdinal, const char* axis)
        {
            // Wrap/Clamp/Mirror all map onto a real GL wrap enum -- see
            // ApplyNanoVgImageSamplerState. Anything else is not a TextureAddressMode at all.
            if (textureAddressModeOrdinal < 0 || textureAddressModeOrdinal > 2)
            {
                throw std::runtime_error(
                    std::string("NANOVG: unsupported TextureAddressMode ordinal ") +
                    std::to_string(textureAddressModeOrdinal) + " for " + axis + ".");
            }
        }

        /// The paint colour whose NanoVG-side premultiplication lands on exactly @p tint.
        ///
        /// nanovg_gl.h's `glnvg__convertPaint` uploads `glnvg__premulColor(paint->innerColor)`,
        /// i.e. `(r*a, g*a, b*a, a)`, and the fragment shader multiplies the sampled texel by that
        /// uniform. XNA's SpriteBatch instead multiplies the texel by the tint component-wise, with
        /// the tint's alpha touching only the alpha channel -- so the RGB handed to NanoVG is
        /// pre-divided by the tint's own alpha, leaving the product equal to the tint itself.
        ///
        /// A tint alpha of exactly zero cannot be inverted, and NanoVG's premultiply would then
        /// erase the tint's RGB entirely -- which is observable, because a zero source alpha does
        /// not imply a zero source colour under Opaque (One, Zero) or AlphaBlend (One,
        /// InverseSourceAlpha). The alpha used for the round trip is therefore floored at a value
        /// far below one 8-bit step, which preserves the RGB exactly while contributing at most
        /// 0.004 of a step to any alpha the blend stage computes from it.
        NVGcolor PaintColorForTint(const Color& tint)
        {
            constexpr float kAlphaFloor = 1.0f / 65536.0f;
            const float tintAlpha = static_cast<float>(tint.getAProperty()) / 255.0f;
            const float roundTripAlpha = std::max(tintAlpha, kAlphaFloor);
            return nvgRGBAf(static_cast<float>(tint.getRProperty()) / 255.0f / roundTripAlpha,
                            static_cast<float>(tint.getGProperty()) / 255.0f / roundTripAlpha,
                            static_cast<float>(tint.getBProperty()) / 255.0f / roundTripAlpha,
                            roundTripAlpha);
        }
    }

    NanoVgSpriteBatchRenderer::NanoVgSpriteBatchRenderer(NanoVgRenderer& owner) : owner_(owner) {}

    NanoVgSpriteBatchRenderer::~NanoVgSpriteBatchRenderer() = default;

    void NanoVgSpriteBatchRenderer::Begin()
    {
        begun_ = true;
        lastSamplerImage_ = 0;
        owner_.EnsureSurfaceSizeEXT();

        const NanoVgRenderer::LogicalViewport viewport = owner_.ComputeLogicalViewportEXT();
        const float logicalW = viewport.logicalWidth > 0.0f ? viewport.logicalWidth : 1.0f;
        const float logicalH = viewport.logicalHeight > 0.0f ? viewport.logicalHeight : 1.0f;
        const float ratio = logicalW > 0.0f ? viewport.width / logicalW : 1.0f;

        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgBeginFrame(ctx, logicalW, logicalH, ratio > 0.0f ? ratio : 1.0f);

        // nvgBeginFrame resets the state stack, so the blend factors have to be re-asserted here.
        // Not nvgGlobalCompositeOperation: NanoVG's composite presets cannot express BlendState's
        // independent colour/alpha factor pairs -- see BlendStateToNvgBlendFunc's own comment.
        const NanoVgBlendFunc blend = owner_.GetBlendFuncEXT();
        nvgGlobalCompositeBlendFuncSeparate(ctx, blend.srcRGB, blend.dstRGB,
                                            blend.srcAlpha, blend.dstAlpha);

        int sx = 0, sy = 0, sw = 0, sh = 0;
        bool scissorEnabled = false;
        owner_.GetScissorEXT(sx, sy, sw, sh, scissorEnabled);
        if (scissorEnabled)
            nvgScissor(ctx, static_cast<float>(sx), static_cast<float>(sy),
                      static_cast<float>(sw), static_cast<float>(sh));

        if (transform_[0] != 1 || transform_[1] != 0 || transform_[2] != 0 ||
            transform_[3] != 1 || transform_[4] != 0 || transform_[5] != 0)
        {
            nvgTransform(ctx, transform_[0], transform_[1], transform_[2],
                        transform_[3], transform_[4], transform_[5]);
        }
    }

    void NanoVgSpriteBatchRenderer::End()
    {
        owner_.MakeContextCurrentEXT();
        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgEndFrame(ctx);

        begun_ = false;
        lastSamplerImage_ = 0;
        transform_[0] = 1; transform_[1] = 0; transform_[2] = 0;
        transform_[3] = 1; transform_[4] = 0; transform_[5] = 0;
    }

    void NanoVgSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_[0] = m.M11; transform_[1] = m.M12;
        transform_[2] = m.M21; transform_[3] = m.M22;
        transform_[4] = m.M41; transform_[5] = m.M42;
    }

    void NanoVgSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "NANOVG does not support custom SpriteBatch Effects: no caller-addressable "
                "programmable shader stage exists on this renderer.");
    }

    void NanoVgSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // Decoded (and so validated) here rather than at the first Draw(): SpriteBatch::Begin()
        // calls this before the renderer's own Begin() and treats a throw as "this batch never
        // began", which is the behaviour an unsupported filter should produce.
        bool minifyPoint = false, magnifyPoint = false;
        DecodeTextureFilter(textureFilter, minifyPoint, magnifyPoint);
        sampler_.minifyPoint = minifyPoint;
        sampler_.magnifyPoint = magnifyPoint;
        lastSamplerImage_ = 0;
    }

    void NanoVgSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        ValidateAddressMode(addressU, "addressU");
        ValidateAddressMode(addressV, "addressV");
        sampler_.addressU = addressU;
        sampler_.addressV = addressV;
        lastSamplerImage_ = 0;
    }

    void NanoVgSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        Draw(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void NanoVgSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void NanoVgSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float /*layerDepth*/)
    {
        if (!begun_)
            throw std::runtime_error("NanoVgSpriteBatchRenderer::Draw called before Begin().");

        owner_.MakeContextCurrentEXT();

        const auto* tex = dynamic_cast<const NanoVgTextureRenderer*>(&texture);
        if (!tex) return;

        const int sx = sourceRectangle.X, sy = sourceRectangle.Y;
        const int sw = sourceRectangle.Width, sh = sourceRectangle.Height;
        const int destW = destinationRectangle.Width, destH = destinationRectangle.Height;
        if (sw <= 0 || sh <= 0 || destW == 0 || destH == 0) return;

        const int texW = tex->GetWidth(), texH = tex->GetHeight();

        // The batch's SamplerState is written onto this image's GL texture object rather than
        // baked into the image at creation time, so the same texture can be drawn point-filtered
        // in one batch and linear-filtered in the next. Safe here because NanoVG only records
        // draw calls until nvgEndFrame and binds textures at flush time, and the batch's sampler
        // is fixed for the whole Begin()/End() pair -- see ApplyNanoVgImageSamplerState's own doc.
        if (tex->GetImageHandle() != lastSamplerImage_)
        {
            ApplyNanoVgImageSamplerState(owner_.GetNvgContextEXT(), tex->GetImageHandle(), sampler_);
            lastSamplerImage_ = tex->GetImageHandle();
        }

        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgSave(ctx);

        // A sprite quad is not a vector shape: XNA's SpriteBatch has no coverage antialiasing at
        // all (this renderer never creates a multisample-capable context either), while NanoVG's
        // NVG_ANTIALIAS fill insets the polygon by half a pixel and feathers the missing half over
        // a separate fringe strip. Disabling shape antialiasing for the fill below makes
        // nvg__expandFill emit the path's own vertices verbatim, so GL's ordinary
        // pixel-centre-inside coverage rule decides each pixel -- the same rule XNA uses. Scoped
        // by the nvgSave/nvgRestore around this draw, so genuine NanoVG vector work elsewhere on
        // the same context keeps its antialiasing.
        nvgShapeAntiAlias(ctx, 0);

        // translate -> rotate -> scale -> flip, identical composition order to
        // CanvasSpriteBatchRenderer's own CNA_Canvas2D_DrawSprite (verified there against FNA's
        // real GenerateVertexInfo placement formula). No device-flip: NanoVG is already
        // top-left-origin/Y-down, matching XNA directly.
        nvgTranslate(ctx, static_cast<float>(destinationRectangle.X), static_cast<float>(destinationRectangle.Y));
        nvgRotate(ctx, rotation);
        nvgScale(ctx, static_cast<float>(destW) / static_cast<float>(sw),
                static_cast<float>(destH) / static_cast<float>(sh));

        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        if (flipH)
        {
            const float cx = -origin.X + static_cast<float>(sw) / 2.0f;
            nvgTranslate(ctx, cx, 0.0f); nvgScale(ctx, -1.0f, 1.0f); nvgTranslate(ctx, -cx, 0.0f);
        }
        if (flipV)
        {
            const float cy = -origin.Y + static_cast<float>(sh) / 2.0f;
            nvgTranslate(ctx, 0.0f, cy); nvgScale(ctx, 1.0f, -1.0f); nvgTranslate(ctx, 0.0f, -cy);
        }

        // The context already carries the destW/sw, destH/sh scale (nvgScale above) -- everything
        // from here is in SOURCE-pixel-scaled local coordinates, same as Canvas2D's own drawImage
        // call. The pattern box (ox,oy,ex,ey) describes where the texture's FULL, UNCROPPED extent
        // would map: positioning it at (-origin.X - sx, -origin.Y - sy) with size (texW, texH)
        // makes source pixel (sx,sy) land exactly at local (-origin.X, -origin.Y), where the fill
        // rect below starts -- no CPU-side sub-image copy needed (unlike OpenVG's vgCopyImage
        // workaround). Texels the fill reaches outside [0,texW]x[0,texH] resolve through the GL
        // wrap mode this batch's own TextureAddressMode selected, applied above.
        const float ox = -origin.X - static_cast<float>(sx);
        const float oy = -origin.Y - static_cast<float>(sy);
        NVGpaint paint = nvgImagePattern(ctx, ox, oy, static_cast<float>(texW), static_cast<float>(texH),
                                         0.0f, tex->GetImageHandle(), 1.0f);
        paint.innerColor = paint.outerColor = PaintColorForTint(color);

        nvgBeginPath(ctx);
        nvgRect(ctx, -origin.X, -origin.Y, static_cast<float>(sw), static_cast<float>(sh));
        nvgFillPaint(ctx, paint);
        nvgFill(ctx);

        nvgRestore(ctx);
    }
}
