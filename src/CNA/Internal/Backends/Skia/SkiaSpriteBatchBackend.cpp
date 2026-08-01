#include "CNA/Internal/Backends/Skia/SkiaSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"

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
        if (transformMatrix_ != Matrix::getIdentityProperty())
            throw std::runtime_error("Skia raster SpriteBatch transform matrices are not implemented yet.");
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0
            || destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
        {
            return;
        }

        const auto* skiaTexture = dynamic_cast<const SkiaTextureBackend*>(&texture);
        if (!skiaTexture || !skiaTexture->Image())
            throw std::runtime_error("Skia SpriteBatch can only draw Skia Texture2D resources.");
        if (sourceRectangle.X < 0 || sourceRectangle.Y < 0
            || sourceRectangle.X > skiaTexture->GetWidth() - sourceRectangle.Width
            || sourceRectangle.Y > skiaTexture->GetHeight() - sourceRectangle.Height)
        {
            throw std::runtime_error("Skia SpriteBatch source rectangle is outside the texture while Clamp addressing is active.");
        }

        (void)layerDepth; // Shared SpriteBatch ordering determines call order before this backend is invoked.
        SkCanvas* canvas = surface_.Canvas();
        if (!canvas)
            throw std::runtime_error("Skia SpriteBatch has no active raster canvas.");

        SkPaint paint;
        const SkColor tint = SkColorSetARGB(color.getAProperty(), color.getRProperty(),
                                            color.getGProperty(), color.getBProperty());
        if (tint != SK_ColorWHITE)
            paint.setColorFilter(SkColorFilters::Blend(tint, SkBlendMode::kModulate));

        const float sourceWidth = static_cast<float>(sourceRectangle.Width);
        const float sourceHeight = static_cast<float>(sourceRectangle.Height);
        SkAutoCanvasRestore restore(canvas, true);
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
        canvas->drawImageRect(skiaTexture->Image().get(), source, destination, ToSampling(textureFilter_),
                              &paint, SkCanvas::kStrict_SrcRectConstraint);
    }
} // namespace CNA::Internal::Backends::Skia
