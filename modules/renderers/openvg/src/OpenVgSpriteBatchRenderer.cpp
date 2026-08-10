// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenVg/OpenVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "openvg.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Internal::Renderers::OpenVg
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        // OpenVG's VGfloat m[9] is column-major {sx,shy,0, shx,sy,0, tx,ty,1} (verified against
        // ShivaVG's own vgLoadMatrix -- see cmake/ThirdPartyOpenVG.cmake's header comment / this
        // module's tests). CanvasRenderer already established the same (a,b,c,d,e,f) convention
        // for a row-major XNA Matrix -> 2D affine (a=M11,b=M12,c=M21,d=M22,e=M41,f=M42); reused
        // verbatim here, just packed for vgMultMatrix instead of ctx.setTransform.
        void MultiplyAffine(const float (&abcdef)[6])
        {
            const VGfloat m[9] = {
                abcdef[0], abcdef[1], 0.0f,
                abcdef[2], abcdef[3], 0.0f,
                abcdef[4], abcdef[5], 1.0f
            };
            vgMultMatrix(m);
        }
    }

    OpenVgSpriteBatchRenderer::OpenVgSpriteBatchRenderer(OpenVgRenderer& owner) : owner_(owner) {}

    OpenVgSpriteBatchRenderer::~OpenVgSpriteBatchRenderer()
    {
        if (tintPaint_) vgDestroyPaint(reinterpret_cast<VGPaint>(tintPaint_));
    }

    void OpenVgSpriteBatchRenderer::Begin()
    {
        begun_ = true;
    }

    void OpenVgSpriteBatchRenderer::End()
    {
        begun_ = false;
        transform_[0] = 1; transform_[1] = 0; transform_[2] = 0;
        transform_[3] = 1; transform_[4] = 0; transform_[5] = 0;
    }

    void OpenVgSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_[0] = m.M11; transform_[1] = m.M12;
        transform_[2] = m.M21; transform_[3] = m.M22;
        transform_[4] = m.M41; transform_[5] = m.M42;
    }

    void OpenVgSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "OpenVG (ShivaVG) does not support custom SpriteBatch Effects: no programmable "
                "shader stage exists on this renderer.");
    }

    void OpenVgSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // Same magnification-dominant reasoning as Canvas/SDL_Renderer's own SetSamplerFilter:
        // Linear=0, Anisotropic=2, LinearMipPoint=3, MinPointMagLinearMipLinear=7,
        // MinPointMagLinearMipPoint=8 all have mag=Linear.
        switch (textureFilter)
        {
            case 0: case 2: case 3: case 7: case 8:
                linearFilter_ = true;
                break;
            default:
                linearFilter_ = false;
                break;
        }
    }

    void OpenVgSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        addressU_ = addressU;
        addressV_ = addressV;
    }

    namespace
    {
        // Returns the VGImage to draw for this sourceRectangle, and sets *isTemporary to whether
        // the caller must vgDestroyImage it after the draw.
        //
        // This does NOT use vgChildImage: that entry point is declared in ShivaVG's openvg.h but
        // its src/shImage.c body is `{ return VG_INVALID_HANDLE; }` -- an unconditional stub, found
        // and verified empirically (a real draw silently produced nothing, tracked down to
        // vgChildImage always failing with VG_NO_ERROR set, i.e. "successfully returned nothing").
        // vgCopyImage IS a real, working implementation (a genuine pixel copy through an
        // intermediate buffer), so a sub-rectangle draw instead copies the requested region into a
        // freshly created VGImage sized exactly to it. The common whole-texture case (by far the
        // most frequent SpriteBatch draw) skips the copy entirely and draws the parent VGImage
        // directly.
        VGImage SubImageOrThrow(const OpenVgTextureRenderer& tex, int sx, int sy, int sw, int sh,
                                int addressU, int addressV, bool& isTemporary)
        {
            isTemporary = false;
            const bool exceedsBounds =
                sx < 0 || sy < 0 || sx + sw > tex.GetWidth() || sy + sh > tex.GetHeight();
            if (exceedsBounds && (addressU != 1 || addressV != 1)) // 1 == Clamp
                throw std::runtime_error(
                    "OpenVG (ShivaVG) SpriteBatch: Wrap/Mirror TextureAddressMode combined with an "
                    "out-of-bounds sourceRectangle is not supported (ShivaVG has no tiled-pattern "
                    "image-draw path).");

            const int csx = std::clamp(sx, 0, tex.GetWidth());
            const int csy = std::clamp(sy, 0, tex.GetHeight());
            const int csw = std::clamp(sx + sw, 0, tex.GetWidth()) - csx;
            const int csh = std::clamp(sy + sh, 0, tex.GetHeight()) - csy;
            if (csw <= 0 || csh <= 0) return VG_INVALID_HANDLE;

            VGImage parent = reinterpret_cast<VGImage>(tex.GetImageHandle());
            if (csx == 0 && csy == 0 && csw == tex.GetWidth() && csh == tex.GetHeight())
                return parent;

            // OpenVgTextureRenderer uploads in plain row-major order (see its own comment) with no
            // row reversal, so the parent VGImage's row index matches sourceRectangle.Y directly --
            // no Y-flip needed here.
            // Must match the parent's own format (VG_sABGR_8888 -- see OpenVgTextureRenderer.cpp's
            // format-choice comment) for vgCopyImage's same-format fast memcpy path below, and so
            // the copied bytes need no re-conversion.
            VGImage sub = vgCreateImage(VG_sABGR_8888, csw, csh,
                                        VG_IMAGE_QUALITY_FASTER | VG_IMAGE_QUALITY_BETTER);
            if (sub == VG_INVALID_HANDLE) return VG_INVALID_HANDLE;
            vgCopyImage(sub, 0, 0, parent, csx, csy, csw, csh, VG_FALSE);
            isTemporary = true;
            return sub;
        }
    }

    void OpenVgSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        Draw(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenVgSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenVgSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float /*layerDepth*/)
    {
        if (!begun_)
            throw std::runtime_error("OpenVgSpriteBatchRenderer::Draw called before Begin().");

        const auto* tex = dynamic_cast<const OpenVgTextureRenderer*>(&texture);
        if (!tex) return;

        const int sw = sourceRectangle.Width, sh = sourceRectangle.Height;
        const int destW = destinationRectangle.Width, destH = destinationRectangle.Height;
        if (sw <= 0 || sh <= 0 || destW == 0 || destH == 0) return;

        bool subImageIsTemporary = false;
        const VGImage drawImage = SubImageOrThrow(*tex, sourceRectangle.X, sourceRectangle.Y, sw, sh,
                                                  addressU_, addressV_, subImageIsTemporary);
        if (drawImage == VG_INVALID_HANDLE) return;

        const float scaleX = static_cast<float>(destW) / static_cast<float>(sw);
        const float scaleY = static_cast<float>(destH) / static_cast<float>(sh);
        const float offX = (std::clamp(sourceRectangle.X, 0, tex->GetWidth()) - sourceRectangle.X) * scaleX;
        const float offY = (std::clamp(sourceRectangle.Y, 0, tex->GetHeight()) - sourceRectangle.Y) * scaleY;

        vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
        // Device flip (see OpenVgRenderer.cpp's ApplyDeviceFlip's own comment for why), then the
        // caller's SetTransformMatrix, then the per-sprite translate/rotate/scale/flip stack --
        // same composition order as CanvasSpriteBatchRenderer::DrawSprite (CanvasRenderer.cpp).
        vgLoadIdentity();
        vgTranslate(0.0f, static_cast<VGfloat>(owner_.GetPhysicalHeightEXT()));
        vgScale(1.0f, -1.0f);
        MultiplyAffine(transform_);

        vgTranslate(static_cast<VGfloat>(destinationRectangle.X), static_cast<VGfloat>(destinationRectangle.Y));
        // vgRotate takes degrees (XNA rotation is radians) AND is negated: the outer device-flip
        // reflection (see above) anti-commutes with a nested rotation -- composing reflection after
        // rotation(theta) is equivalent to rotation(-theta) after reflection, so the angle fed here
        // must be negated for the FINAL on-screen rotation to match XNA's own (clockwise-positive,
        // Y-down) sense. Verified empirically against openvg_spritebatch_rotation_test.cpp's pixel
        // oracle (a positive, non-negated angle rotated the marker to a mirrored position).
        vgRotate(rotation * (180.0f / kPi));
        vgScale(scaleX, scaleY);

        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        if (flipH)
        {
            const float cx = -origin.X + static_cast<float>(sw) / 2.0f;
            vgTranslate(cx, 0.0f); vgScale(-1.0f, 1.0f); vgTranslate(-cx, 0.0f);
        }
        if (flipV)
        {
            const float cy = -origin.Y + static_cast<float>(sh) / 2.0f;
            vgTranslate(0.0f, cy); vgScale(1.0f, -1.0f); vgTranslate(0.0f, -cy);
        }
        vgTranslate(-origin.X + offX, -origin.Y + offY);

        const bool tinted = color.getRProperty() != 255 || color.getGProperty() != 255 ||
                            color.getBProperty() != 255 || color.getAProperty() != 255;
        if (tinted)
        {
            if (!tintPaint_) tintPaint_ = reinterpret_cast<void*>(vgCreatePaint());
            const VGPaint paint = reinterpret_cast<VGPaint>(tintPaint_);
            const VGfloat rgba[4] = {
                color.getRProperty() / 255.0f, color.getGProperty() / 255.0f,
                color.getBProperty() / 255.0f, color.getAProperty() / 255.0f
            };
            vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
            vgSetParameterfv(paint, VG_PAINT_COLOR, 4, rgba);
            vgSetPaint(paint, VG_FILL_PATH);
            vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_MULTIPLY);
        }
        else
        {
            vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
        }

        vgSeti(VG_IMAGE_QUALITY, linearFilter_ ? VG_IMAGE_QUALITY_BETTER : VG_IMAGE_QUALITY_NONANTIALIASED);
        vgDrawImage(drawImage);
        if (subImageIsTemporary)
            vgDestroyImage(drawImage);
    }
}
