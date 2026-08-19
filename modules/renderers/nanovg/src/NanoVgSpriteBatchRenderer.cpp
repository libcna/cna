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

        /// Clips a convex polygon to an axis-aligned rectangle (Sutherland-Hodgman against four
        /// half-planes). Axis-aligned clip edges make this orientation-independent, so a sprite
        /// whose transform flips it -- every `SpriteEffects` flip does -- needs no special case.
        ///
        /// @param points In/out polygon vertices; at most 8 are ever produced from a quad, because
        ///        each of the four half-planes adds at most one vertex.
        /// @param count Number of valid vertices on entry.
        /// @param minX,minY,maxX,maxY The clip rectangle.
        /// @return The number of vertices left; fewer than three means nothing survives.
        int ClipConvexToRect(float points[8][2], int count,
                             float minX, float minY, float maxX, float maxY)
        {
            // edge 0: x >= minX, 1: x <= maxX, 2: y >= minY, 3: y <= maxY
            for (int edge = 0; edge < 4 && count > 0; ++edge)
            {
                const auto inside = [&](const float* p)
                {
                    switch (edge)
                    {
                    case 0:  return p[0] >= minX;
                    case 1:  return p[0] <= maxX;
                    case 2:  return p[1] >= minY;
                    default: return p[1] <= maxY;
                    }
                };
                const auto intersect = [&](const float* a, const float* b, float* out)
                {
                    // Parameter along a->b at which the edge is crossed. The denominators cannot be
                    // zero here: `inside` differs for a and b, so the relevant component differs.
                    float t = 0.0f;
                    switch (edge)
                    {
                    case 0:  t = (minX - a[0]) / (b[0] - a[0]); break;
                    case 1:  t = (maxX - a[0]) / (b[0] - a[0]); break;
                    case 2:  t = (minY - a[1]) / (b[1] - a[1]); break;
                    default: t = (maxY - a[1]) / (b[1] - a[1]); break;
                    }
                    out[0] = a[0] + t * (b[0] - a[0]);
                    out[1] = a[1] + t * (b[1] - a[1]);
                };

                float output[8][2];
                int produced = 0;
                for (int i = 0; i < count && produced < 8; ++i)
                {
                    const float* current = points[i];
                    const float* previous = points[(i + count - 1) % count];
                    const bool currentIn = inside(current);
                    const bool previousIn = inside(previous);
                    if (currentIn)
                    {
                        if (!previousIn && produced < 8)
                            intersect(previous, current, output[produced++]);
                        if (produced < 8)
                        {
                            output[produced][0] = current[0];
                            output[produced][1] = current[1];
                            ++produced;
                        }
                    }
                    else if (previousIn && produced < 8)
                    {
                        intersect(previous, current, output[produced++]);
                    }
                }
                count = produced;
                for (int i = 0; i < count; ++i)
                {
                    points[i][0] = output[i][0];
                    points[i][1] = output[i][1];
                }
            }
            return count;
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

    void NanoVgSpriteBatchRenderer::BeginFrameForProjection(
        const NanoVgRenderer::SpriteProjection& projection)
    {
        NVGcontext* ctx = owner_.GetNvgContextEXT();
        nvgBeginFrame(ctx, projection.width, projection.height,
                      projection.devicePixelRatio > 0.0f ? projection.devicePixelRatio : 1.0f);
        projection_ = projection;

        // nvgBeginFrame resets the state stack, so the blend factors have to be re-asserted here.
        // Not nvgGlobalCompositeOperation: NanoVG's composite presets cannot express BlendState's
        // independent colour/alpha factor pairs -- see BlendStateToNvgBlendFunc's own comment.
        ApplyCurrentBlendFunc();

        if (transform_[0] != 1 || transform_[1] != 0 || transform_[2] != 0 ||
            transform_[3] != 1 || transform_[4] != 0 || transform_[5] != 0)
        {
            nvgTransform(ctx, transform_[0], transform_[1], transform_[2],
                        transform_[3], transform_[4], transform_[5]);
        }
    }

    void NanoVgSpriteBatchRenderer::ApplyCurrentBlendFunc()
    {
        const NanoVgBlendFunc blend = owner_.GetBlendFuncEXT();
        nvgGlobalCompositeBlendFuncSeparate(owner_.GetNvgContextEXT(), blend.srcRGB, blend.dstRGB,
                                            blend.srcAlpha, blend.dstAlpha);
        appliedBlend_ = blend;
    }

    void NanoVgSpriteBatchRenderer::Begin()
    {
        begun_ = true;
        lastSamplerImage_ = 0;
        owner_.EnsureSurfaceSizeEXT();

        // The sprite coordinate space is the ACTIVE GraphicsDevice.Viewport's, not the whole
        // drawable -- see NanoVgRenderer::GetSpriteProjectionEXT's own doc comment.
        BeginFrameForProjection(owner_.GetSpriteProjectionEXT());

        // Deliberately NOT nvgScissor(): NanoVG's scissor is a SHADER MASK that multiplies the
        // fragment colour (`color *= scissor` in nanovg_gl.h's own fragment shader), not a
        // rasterizer clip. A masked-out fragment therefore still WRITES -- it writes zero -- so
        // under any BlendState whose destination factor does not evaluate to one for a zero
        // source (BlendState.Opaque's Zero, most obviously) the "clipped" region would be
        // blackened rather than left alone. Each Draw() clips its own quad geometrically instead,
        // which is exact for every BlendState and, as a bonus, hard-edged rather than carrying the
        // shader mask's own one-pixel feather.
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

    void NanoVgSpriteBatchRenderer::SetImmediateMode(bool immediate)
    {
        immediate_ = immediate;
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

        // SpriteSortMode::Immediate promises that device state changed BETWEEN two Draw() calls is
        // reflected per sprite (ISpriteBatchRenderer::SetImmediateMode's own contract), so the
        // state captured at Begin() has to be re-read here. Deferred keeps the batch-snapshot
        // semantics every other renderer establishes: all of its Draw() calls run from End()
        // anyway, under one device state. Safe to re-issue nvgBeginFrame at this point because the
        // previous Immediate Draw() already flushed, so no recorded call can be lost -- and it is
        // done BEFORE the nvgSave below, so the state stack nvgBeginFrame resets is not one this
        // call is in the middle of using. The scissor rectangle needs no equivalent: Draw() reads
        // it from the owner on every call already.
        if (immediate_)
        {
            const NanoVgRenderer::SpriteProjection projection = owner_.GetSpriteProjectionEXT();
            if (projection.width != projection_.width || projection.height != projection_.height ||
                projection.customViewport != projection_.customViewport)
            {
                BeginFrameForProjection(projection);
            }
            else
            {
                const NanoVgBlendFunc blend = owner_.GetBlendFuncEXT();
                if (blend.srcRGB != appliedBlend_.srcRGB || blend.dstRGB != appliedBlend_.dstRGB ||
                    blend.srcAlpha != appliedBlend_.srcAlpha ||
                    blend.dstAlpha != appliedBlend_.dstAlpha)
                {
                    ApplyCurrentBlendFunc();
                }
            }
        }

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

        // nvgFillPaint multiplies the CURRENT transform into the paint's own (nanovg.c), anchoring
        // the texture mapping to this local space. Doing it before the path is built is what lets
        // the clipped path below be emitted in logical space without disturbing a single texel.
        nvgFillPaint(ctx, paint);

        int scissorX = 0, scissorY = 0, scissorW = 0, scissorH = 0;
        bool scissorEnabled = false;
        owner_.GetScissorEXT(scissorX, scissorY, scissorW, scissorH, scissorEnabled);

        nvgBeginPath(ctx);
        if (!scissorEnabled)
        {
            nvgRect(ctx, -origin.X, -origin.Y, static_cast<float>(sw), static_cast<float>(sh));
        }
        else
        {
            if (scissorW <= 0 || scissorH <= 0)
            {
                nvgRestore(ctx);
                return;
            }

            // GraphicsDevice.ScissorRectangle is expressed in the render target's own logical
            // space, which is NOT the sprite coordinate space once a custom Viewport makes sprites
            // viewport-local -- see NanoVgRenderer::GetSpriteProjectionEXT.
            const float clipMinX = projection_.scissorOffsetX +
                                   static_cast<float>(scissorX) * projection_.scissorScaleX;
            const float clipMinY = projection_.scissorOffsetY +
                                   static_cast<float>(scissorY) * projection_.scissorScaleY;
            const float clipMaxX = clipMinX + static_cast<float>(scissorW) * projection_.scissorScaleX;
            const float clipMaxY = clipMinY + static_cast<float>(scissorH) * projection_.scissorScaleY;

            float xform[6];
            nvgCurrentTransform(ctx, xform);
            const float lx0 = -origin.X, ly0 = -origin.Y;
            const float lx1 = lx0 + static_cast<float>(sw), ly1 = ly0 + static_cast<float>(sh);
            const float localCorners[4][2] = {{lx0, ly0}, {lx1, ly0}, {lx1, ly1}, {lx0, ly1}};
            float polygon[8][2];
            for (int i = 0; i < 4; ++i)
            {
                nvgTransformPoint(&polygon[i][0], &polygon[i][1], xform,
                                  localCorners[i][0], localCorners[i][1]);
            }
            const int corners = ClipConvexToRect(polygon, 4, clipMinX, clipMinY, clipMaxX, clipMaxY);
            if (corners < 3)
            {
                nvgRestore(ctx);
                return;
            }

            // The clipped outline is already in logical space, so the accumulated sprite transform
            // must not be applied to it a second time. The paint keeps its own copy (see above),
            // and nvgRestore puts the batch's transform back for the next Draw().
            nvgResetTransform(ctx);
            nvgMoveTo(ctx, polygon[0][0], polygon[0][1]);
            for (int i = 1; i < corners; ++i)
                nvgLineTo(ctx, polygon[i][0], polygon[i][1]);
            nvgClosePath(ctx);
        }
        nvgFill(ctx);

        nvgRestore(ctx);

        // SpriteSortMode::Immediate means this sprite must already be on the surface when Draw()
        // returns, so that a GraphicsDevice operation the caller issues before the next Draw() --
        // a Clear(), a render-state change, a readback -- happens AFTER it rather than before the
        // whole batch. NanoVG normally submits only at nvgEndFrame, so the recorded call list is
        // flushed here through the backend's own renderFlush, reached via the public
        // nvgInternalParams(). Deliberately NOT nvgEndFrame()/nvgBeginFrame(): that pair runs
        // nvgReset(), which would discard the batch's scissor, transform and blend factors
        // mid-batch. renderFlush leaves the frame open and simply empties the call list, so the
        // next Draw() accumulates from a clean list into the same frame.
        if (immediate_)
        {
            NVGparams* params = nvgInternalParams(ctx);
            if (params != nullptr && params->renderFlush != nullptr)
                params->renderFlush(params->userPtr);
        }
    }
}
