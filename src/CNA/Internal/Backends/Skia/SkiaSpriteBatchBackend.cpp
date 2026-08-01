#include "CNA/Internal/Backends/Skia/SkiaSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] SkSamplingOptions ToSampling(int textureFilter)
        {
            switch (textureFilter)
            {
                case 0: // Linear
                case 2: // Anisotropic (the raster path has no anisotropy control)
                case 3: // LinearMipPoint
                case 7: // MinPointMagLinearMipLinear
                case 8: // MinPointMagLinearMipPoint
                    return SkSamplingOptions(SkFilterMode::kLinear);
                default:
                    return SkSamplingOptions(SkFilterMode::kNearest);
            }
        }

        [[nodiscard]] bool IsFlipped(SpriteEffects effects, SpriteEffects flag)
        {
            return (static_cast<int>(effects) & static_cast<int>(flag)) != 0;
        }

        [[nodiscard]] SkMatrix ToSkMatrix(const Matrix& matrix)
        {
            // CNA/XNA uses row vectors: Vector2::Transform(x, y, matrix) computes
            // (x * M11 + y * M21 + M41, x * M12 + y * M22 + M42). Skia stores the
            // equivalent affine transform for column vectors in its 3x3 matrix. This is
            // deliberately limited to the same six members that the public Vector2 helper
            // uses, so unrelated 3D members cannot change a SpriteBatch's 2D geometry.
            SkMatrix result;
            result.setAll(matrix.M11, matrix.M21, matrix.M41,
                          matrix.M12, matrix.M22, matrix.M42,
                          0.0f,       0.0f,       1.0f);
            return result;
        }

        struct TintScale
        {
            float red;
            float green;
            float blue;
            float alpha;
        };

        [[nodiscard]] const sk_sp<SkRuntimeEffect>& TintEffect()
        {
            // SkColorFilters::Blend(..., kModulate) uses premultiplied tint RGB, which would
            // multiply a premultiplied XNA source by tint alpha a second time.  This filter works
            // directly in Skia's premultiplied pipeline and keeps component-wise XNA tint math.
            static const sk_sp<SkRuntimeEffect> effect = []
            {
                auto result = SkRuntimeEffect::MakeForColorFilter(SkString(R"(
                    uniform float4 tintScale;
                    half4 main(half4 source) {
                        return half4(source.rgb * tintScale.rgb, source.a * tintScale.a);
                    }
                )"));
                if (!result.effect)
                    throw std::runtime_error("Skia failed to compile the SpriteBatch tint color filter.");
                return std::move(result.effect);
            }();
            return effect;
        }

        [[nodiscard]] sk_sp<SkColorFilter> MakeTintFilter(const Color& color,
                                                            SkiaSourceAlphaConvention alphaConvention)
        {
            const float alpha = static_cast<float>(color.getAProperty()) / 255.0f;
            // Skia has already premultiplied a straight-alpha image when the filter sees it;
            // XNA's NonPremultiplied equation still needs that source alpha after tinting.
            const float straightAlphaScale = alphaConvention == SkiaSourceAlphaConvention::Straight
                ? alpha
                : 1.0f;
            const TintScale scale {
                static_cast<float>(color.getRProperty()) / 255.0f * straightAlphaScale,
                static_cast<float>(color.getGProperty()) / 255.0f * straightAlphaScale,
                static_cast<float>(color.getBProperty()) / 255.0f * straightAlphaScale,
                alpha,
            };
            return TintEffect()->makeColorFilter(SkData::MakeWithCopy(&scale, sizeof(scale)));
        }
    }

    void SkiaSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::runtime_error("Skia SpriteBatch Begin() was called without a matching End().");
        begun_ = true;
    }

    void SkiaSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::runtime_error("Skia SpriteBatch End() was called without a matching Begin().");
        begun_ = false;
    }

    void SkiaSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect)
            throw std::runtime_error("Skia raster SpriteBatch custom Effects are not implemented yet.");
    }

    void SkiaSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        textureFilter_ = textureFilter;
    }

    void SkiaSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        // SkCanvas's source-rectangle drawing is a direct clamp operation. Tile-mode shaders are
        // the next implementation step; accepting Wrap/Mirror here would make their semantics
        // silently disappear for out-of-bounds source rectangles.
        if (addressU != 1 || addressV != 1)
            throw std::runtime_error("Skia raster SpriteBatch currently supports only Clamp texture addressing.");
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void SkiaSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void SkiaSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void SkiaSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color, float rotation,
                                      const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        if (!begun_)
            throw std::runtime_error("Skia SpriteBatch Draw() was called before Begin().");
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0
            || destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
        {
            return;
        }

        const auto* skiaImageSource = dynamic_cast<const SkiaImageSource*>(&texture);
        const SkiaSourceAlphaConvention sourceAlphaConvention = sourceAlphaConvention_
            ? *sourceAlphaConvention_
            : SkiaSourceAlphaConvention::Premultiplied;
        const sk_sp<SkImage> image = skiaImageSource
            ? skiaImageSource->SnapshotImage(sourceAlphaConvention)
            : nullptr;
        if (!image)
            throw std::runtime_error("Skia SpriteBatch can only draw Skia Texture2D or RenderTarget2D resources.");
        if (sourceRectangle.X < 0 || sourceRectangle.Y < 0
            || sourceRectangle.X > texture.GetWidth() - sourceRectangle.Width
            || sourceRectangle.Y > texture.GetHeight() - sourceRectangle.Height)
        {
            throw std::runtime_error("Skia SpriteBatch source rectangle is outside the texture while Clamp addressing is active.");
        }

        (void)layerDepth; // Shared SpriteBatch ordering determines call order before this backend is invoked.
        SkCanvas* canvas = activeSurface_ && *activeSurface_ ? (*activeSurface_)->Canvas() : nullptr;
        if (!canvas)
            throw std::runtime_error("Skia SpriteBatch has no active raster canvas.");

        SkPaint paint;
        paint.setBlendMode(blendMode_ ? *blendMode_ : SkBlendMode::kSrcOver);
        if (color != Color::White)
            paint.setColorFilter(MakeTintFilter(color, sourceAlphaConvention));

        const float sourceWidth = static_cast<float>(sourceRectangle.Width);
        const float sourceHeight = static_cast<float>(sourceRectangle.Height);
        SkAutoCanvasRestore restore(canvas, true);
        if (rasterState_ && rasterState_->viewportSet)
        {
            // XNA's viewport is target-space and SpriteBatch coordinates are local to it. Clip
            // in target space before installing any per-sprite transform, then move the local
            // origin to the viewport's top-left corner below.
            canvas->clipRect(SkRect::MakeXYWH(static_cast<float>(rasterState_->viewportX),
                                               static_cast<float>(rasterState_->viewportY),
                                               static_cast<float>(rasterState_->viewportWidth),
                                               static_cast<float>(rasterState_->viewportHeight)),
                             SkClipOp::kIntersect, false);
        }
        if (rasterState_ && rasterState_->scissorTestEnabled)
        {
            // ScissorRectangle lives in active-target pixel coordinates, not in the SpriteBatch
            // transform's local coordinates. Clip before applying the Begin transform so its
            // edges remain axis-aligned and exact after any rotation/scale of a sprite.
            canvas->clipRect(SkRect::MakeXYWH(static_cast<float>(rasterState_->scissorX),
                                               static_cast<float>(rasterState_->scissorY),
                                               static_cast<float>(rasterState_->scissorWidth),
                                               static_cast<float>(rasterState_->scissorHeight)),
                             SkClipOp::kIntersect, false);
        }
        if (rasterState_ && rasterState_->viewportSet)
            canvas->translate(static_cast<float>(rasterState_->viewportX),
                              static_cast<float>(rasterState_->viewportY));
        // SkCanvas::concat pre-concatenates the matrix, making each following local sprite
        // transform run first and this Begin transform run second. That is XNA's ordering:
        // generate the sprite corner, then pass it through Vector2::Transform(matrix).
        canvas->concat(ToSkMatrix(transformMatrix_));
        canvas->translate(static_cast<float>(destinationRectangle.X), static_cast<float>(destinationRectangle.Y));
        canvas->rotate(rotation * 180.0f / 3.14159265358979323846f);
        canvas->scale(static_cast<float>(destinationRectangle.Width) / sourceWidth,
                      static_cast<float>(destinationRectangle.Height) / sourceHeight);

        const float localCenterX = -origin.X + sourceWidth * 0.5f;
        const float localCenterY = -origin.Y + sourceHeight * 0.5f;
        if (IsFlipped(effects, SpriteEffects::FlipHorizontally))
        {
            canvas->translate(localCenterX, 0.0f);
            canvas->scale(-1.0f, 1.0f);
            canvas->translate(-localCenterX, 0.0f);
        }
        if (IsFlipped(effects, SpriteEffects::FlipVertically))
        {
            canvas->translate(0.0f, localCenterY);
            canvas->scale(1.0f, -1.0f);
            canvas->translate(0.0f, -localCenterY);
        }

        const SkRect source = SkRect::MakeXYWH(static_cast<float>(sourceRectangle.X),
                                                static_cast<float>(sourceRectangle.Y),
                                                sourceWidth, sourceHeight);
        const SkRect destination = SkRect::MakeXYWH(-origin.X, -origin.Y, sourceWidth, sourceHeight);
        canvas->drawImageRect(image.get(), source, destination, ToSampling(textureFilter_),
                              &paint, SkCanvas::kStrict_SrcRectConstraint);
    }
} // namespace CNA::Internal::Backends::Skia
