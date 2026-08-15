// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenVg/OpenVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "openvg.h"

// P1-8: see OpenVgRenderer.cpp's own comment -- same portable GL header selection.
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

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
        // Same magnification-dominant reasoning as the Canvas renderer's SetSamplerFilter:
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
        // Returns the VGImage to draw for this sourceRectangle -- always sized exactly `sw`x`sh`
        // (the FULL requested extent, never cropped) so the caller's scaleX=destW/sw,
        // scaleY=destH/sh always applies directly with no separate position/offset correction --
        // and sets *isTemporary to whether the caller must vgDestroyImage it after the draw.
        //
        // This does NOT use vgChildImage: that entry point is declared in ShivaVG's openvg.h but
        // its src/shImage.c body is `{ return VG_INVALID_HANDLE; }` -- an unconditional stub, found
        // and verified empirically (a real draw silently produced nothing, tracked down to
        // vgChildImage always failing with VG_NO_ERROR set, i.e. "successfully returned nothing").
        // vgCopyImage IS a real, working implementation (a genuine pixel copy through an
        // intermediate buffer), so a fully in-bounds sub-rectangle draw copies the requested region
        // into a freshly created VGImage sized exactly to it. The common whole-texture case (by far
        // the most frequent SpriteBatch draw) skips the copy entirely and draws the parent VGImage
        // directly.
        VGImage BuildDrawImage(const OpenVgTextureRenderer& tex, int sx, int sy, int sw, int sh,
                               int addressU, int addressV, bool& isTemporary)
        {
            isTemporary = false;
            const int texW = tex.GetWidth(), texH = tex.GetHeight();
            const bool exceedsBounds = sx < 0 || sy < 0 || sx + sw > texW || sy + sh > texH;

            if (!exceedsBounds)
            {
                if (sx == 0 && sy == 0 && sw == texW && sh == texH)
                    return reinterpret_cast<VGImage>(tex.GetImageHandle());

                // OpenVgTextureRenderer uploads in plain row-major order (see its own comment) with
                // no row reversal, so the parent VGImage's row index matches sourceRectangle.Y
                // directly -- no Y-flip needed here. Must match the parent's own format
                // (VG_sABGR_8888 -- see OpenVgTextureRenderer.cpp's format-choice comment) for
                // vgCopyImage's same-format fast memcpy path, so the copied bytes need no
                // re-conversion.
                VGImage sub = vgCreateImage(VG_sABGR_8888, sw, sh,
                                            VG_IMAGE_QUALITY_FASTER | VG_IMAGE_QUALITY_BETTER);
                if (sub == VG_INVALID_HANDLE) return VG_INVALID_HANDLE;
                vgCopyImage(sub, 0, 0, reinterpret_cast<VGImage>(tex.GetImageHandle()), sx, sy, sw, sh, VG_FALSE);
                isTemporary = true;
                return sub;
            }

            // Out-of-bounds source rectangle. TextureAddressMode.Wrap/Mirror would need a tiled-
            // pattern image-draw path ShivaVG does not have (vgDrawImage has no per-draw wrap-mode
            // parameter at all), so those are rejected deterministically rather than silently
            // cropped or wrapped incorrectly.
            if (addressU != 1 || addressV != 1) // 1 == Clamp
            {
                throw std::runtime_error(
                    "OpenVG (ShivaVG) SpriteBatch: Wrap/Mirror TextureAddressMode combined with an "
                    "out-of-bounds sourceRectangle is not supported (ShivaVG has no tiled-pattern "
                    "image-draw path).");
            }

            // TextureAddressMode.Clamp: build a real edge-clamped image sized exactly sw x sh --
            // every out-of-bounds texel repeats its nearest in-bounds edge texel, genuine
            // clamp-to-edge sampling, NOT a crop. (A plain intersect-and-crop, as this renderer
            // used to do, silently shrinks the visible region instead -- indistinguishable from
            // Clamp only when the excess is fully transparent, and simply wrong otherwise.)
            const int csx = std::clamp(sx, 0, texW);
            const int csy = std::clamp(sy, 0, texH);
            const int csw = std::clamp(sx + sw, 0, texW) - csx;
            const int csh = std::clamp(sy + sh, 0, texH) - csy;
            if (csw <= 0 || csh <= 0) return VG_INVALID_HANDLE;

            std::vector<uint8_t> validPixels(static_cast<std::size_t>(csw) * static_cast<std::size_t>(csh) * 4);
            vgGetImageSubData(reinterpret_cast<VGImage>(tex.GetImageHandle()), validPixels.data(),
                              csw * 4, VG_sABGR_8888, csx, csy, csw, csh);

            std::vector<uint8_t> padded(static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh) * 4);
            for (int dy = 0; dy < sh; ++dy)
            {
                const int srcY = std::clamp(sy + dy, csy, csy + csh - 1) - csy;
                const uint8_t* srcRow = &validPixels[static_cast<std::size_t>(srcY) * csw * 4];
                uint8_t* dstRow = &padded[static_cast<std::size_t>(dy) * sw * 4];
                for (int dx = 0; dx < sw; ++dx)
                {
                    const int srcX = std::clamp(sx + dx, csx, csx + csw - 1) - csx;
                    std::memcpy(dstRow + static_cast<std::size_t>(dx) * 4, srcRow + static_cast<std::size_t>(srcX) * 4, 4);
                }
            }

            VGImage padImg = vgCreateImage(VG_sABGR_8888, sw, sh,
                                           VG_IMAGE_QUALITY_FASTER | VG_IMAGE_QUALITY_BETTER);
            if (padImg == VG_INVALID_HANDLE) return VG_INVALID_HANDLE;
            vgImageSubData(padImg, padded.data(), sw * 4, VG_sABGR_8888, 0, 0, sw, sh);
            isTemporary = true;
            return padImg;
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

        // P0-4: a caller may resize the window and draw immediately, with no Clear()/SetViewport()
        // call in between -- keep ShivaVG's own surface size and this renderer's own
        // ortho/viewport/scissor state current before this draw depends on any of it.
        owner_.EnsureSurfaceSizeEXT();

        bool subImageIsTemporary = false;
        const VGImage drawImage = BuildDrawImage(*tex, sourceRectangle.X, sourceRectangle.Y, sw, sh,
                                                 addressU_, addressV_, subImageIsTemporary);
        if (drawImage == VG_INVALID_HANDLE) return;

        const float scaleX = static_cast<float>(destW) / static_cast<float>(sw);
        const float scaleY = static_cast<float>(destH) / static_cast<float>(sh);

        vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
        // Device flip (see the class doc comment), then the caller's SetTransformMatrix, then the
        // per-sprite translate/rotate/scale/flip stack -- same composition order as
        // CanvasSpriteBatchRenderer::DrawSprite (CanvasRenderer.cpp). Scoped to the current LOGICAL
        // canvas height (P0-1/P0-2), not the physical framebuffer height: GL_PROJECTION's ortho
        // (OpenVgRenderer::applyOrtho) and the active glViewport rectangle already carry the
        // logical->physical presentation mapping, so this transform only ever needs to reach
        // logical/object space.
        vgLoadIdentity();
        vgTranslate(0.0f, static_cast<VGfloat>(owner_.GetLogicalHeightEXT()));
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
        // BuildDrawImage always returns an image sized exactly sw x sh (the full requested extent,
        // clamped-padded when out-of-bounds) -- unlike the crop this renderer used to do, no extra
        // offset is needed here to compensate for a smaller-than-requested image.
        vgTranslate(-origin.X, -origin.Y);

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

        // P1-5: BlendState.ColorWriteChannels[0], honored via a real glColorMask wrapped tightly
        // around this one vgDrawImage call -- see OpenVgRenderer::GetColorWriteMaskEXT's own doc
        // comment for why this is safe (ShivaVG's own vgDrawImage never touches glColorMask on the
        // VG_PAINT_TYPE_COLOR tint path this renderer always uses).
        const bool* mask = owner_.GetColorWriteMaskEXT();
        const bool maskIsDefault = mask[0] && mask[1] && mask[2] && mask[3];
        if (!maskIsDefault)
        {
            glColorMask(mask[0] ? GL_TRUE : GL_FALSE, mask[1] ? GL_TRUE : GL_FALSE,
                       mask[2] ? GL_TRUE : GL_FALSE, mask[3] ? GL_TRUE : GL_FALSE);
        }
        vgDrawImage(drawImage);
        if (!maskIsDefault)
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        if (subImageIsTemporary)
            vgDestroyImage(drawImage);
    }
}
