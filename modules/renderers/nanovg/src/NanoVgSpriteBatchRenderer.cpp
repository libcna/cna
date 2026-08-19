// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "nanovg.h"

#include <stdexcept>

namespace CNA::Internal::Renderers::NanoVg
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    NanoVgSpriteBatchRenderer::NanoVgSpriteBatchRenderer(NanoVgRenderer& owner) : owner_(owner) {}

    NanoVgSpriteBatchRenderer::~NanoVgSpriteBatchRenderer() = default;

    void NanoVgSpriteBatchRenderer::Begin()
    {
        begun_ = true;
        owner_.EnsureSurfaceSizeEXT();

        const NanoVgRenderer::LogicalViewport viewport = owner_.ComputeLogicalViewportEXT();
        const float logicalW = viewport.logicalWidth > 0.0f ? viewport.logicalWidth : 1.0f;
        const float logicalH = viewport.logicalHeight > 0.0f ? viewport.logicalHeight : 1.0f;
        const float ratio = logicalW > 0.0f ? viewport.width / logicalW : 1.0f;

        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgBeginFrame(ctx, logicalW, logicalH, ratio > 0.0f ? ratio : 1.0f);
        nvgGlobalCompositeOperation(ctx, owner_.GetCompositeOperationEXT());

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
        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgEndFrame(ctx);

        begun_ = false;
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

    void NanoVgSpriteBatchRenderer::SetSamplerFilter(int /*textureFilter*/) {}

    void NanoVgSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        addressU_ = addressU;
        addressV_ = addressV;
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

        const auto* tex = dynamic_cast<const NanoVgTextureRenderer*>(&texture);
        if (!tex) return;

        const int sx = sourceRectangle.X, sy = sourceRectangle.Y;
        const int sw = sourceRectangle.Width, sh = sourceRectangle.Height;
        const int destW = destinationRectangle.Width, destH = destinationRectangle.Height;
        if (sw <= 0 || sh <= 0 || destW == 0 || destH == 0) return;

        const int texW = tex->GetWidth(), texH = tex->GetHeight();
        const bool exceedsBounds = sx < 0 || sy < 0 || sx + sw > texW || sy + sh > texH;
        if (exceedsBounds && (addressU_ != 1 || addressV_ != 1)) // 1 == Clamp
        {
            throw std::runtime_error(
                "NANOVG SpriteBatch: Wrap/Mirror TextureAddressMode combined with an "
                "out-of-bounds sourceRectangle is not supported (NanoVG bakes repeat/clamp into "
                "the image at creation time, not per draw).");
        }

        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgSave(ctx);

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
        // workaround). Texels outside [0,texW]x[0,texH] sample via real GL_CLAMP_TO_EDGE (every
        // NanoVgTextureRenderer is created with no NVG_IMAGE_REPEATX/Y flag), which is exactly
        // TextureAddressMode.Clamp's contract -- automatically, with no special-case code.
        const float ox = -origin.X - static_cast<float>(sx);
        const float oy = -origin.Y - static_cast<float>(sy);
        NVGpaint paint = nvgImagePattern(ctx, ox, oy, static_cast<float>(texW), static_cast<float>(texH),
                                         0.0f, tex->GetImageHandle(), 1.0f);
        paint.innerColor = paint.outerColor = nvgRGBA(
            color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty());

        nvgBeginPath(ctx);
        nvgRect(ctx, -origin.X, -origin.Y, static_cast<float>(sw), static_cast<float>(sh));
        nvgFillPaint(ctx, paint);
        nvgFill(ctx);

        nvgRestore(ctx);
    }
}
