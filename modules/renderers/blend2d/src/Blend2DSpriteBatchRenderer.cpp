#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::Renderers::Blend2D
{
    namespace
    {
        /// Builds a new premultiplied BLImage holding the [sx,sy,sw,sh] sub-rectangle of
        /// @p source, tinted by @p color (straight-component multiply, matching XNA
        /// SpriteBatch's per-vertex colour modulation). Blend2D's stock blit_image() has no
        /// per-draw colour modulation of its own, so a non-white tint is realized as a genuine
        /// CPU pixel pass rather than approximated with a post-blit composite operator.
        [[nodiscard]] BLImage BuildTintedSubImageEXT(const BLImage& source, int sx, int sy, int sw,
                                                      int sh, const Color& color)
        {
            BLImageData srcData{};
            if (source.get_data(&srcData) != BL_SUCCESS)
                throw std::runtime_error("Blend2DSpriteBatchRenderer: BLImage::get_data failed");

            BLImage tinted;
            if (tinted.create(sw, sh, BL_FORMAT_PRGB32) != BL_SUCCESS)
                throw std::runtime_error("Blend2DSpriteBatchRenderer: tinted BLImage::create failed");

            BLImageData dstData{};
            if (tinted.make_mutable(&dstData) != BL_SUCCESS)
                throw std::runtime_error("Blend2DSpriteBatchRenderer: tinted BLImage::make_mutable failed");

            const auto tr = static_cast<int>(color.getRProperty());
            const auto tg = static_cast<int>(color.getGProperty());
            const auto tb = static_cast<int>(color.getBProperty());
            const auto ta = static_cast<int>(color.getAProperty());

            const auto* srcBase = static_cast<const std::uint8_t*>(srcData.pixel_data);
            auto* dstBase = static_cast<std::uint8_t*>(dstData.pixel_data);
            std::vector<std::uint8_t> straightRow(static_cast<std::size_t>(sw) * 4);

            for (int row = 0; row < sh; ++row)
            {
                const std::uint8_t* srcRow = srcBase +
                    static_cast<std::ptrdiff_t>(sy + row) * srcData.stride +
                    static_cast<std::ptrdiff_t>(sx) * 4;
                ConvertPremultipliedBgraRowToStraightRgba(srcRow, straightRow.data(), sw);

                for (int col = 0; col < sw; ++col)
                {
                    straightRow[col * 4 + 0] =
                        static_cast<std::uint8_t>((straightRow[col * 4 + 0] * tr + 127) / 255);
                    straightRow[col * 4 + 1] =
                        static_cast<std::uint8_t>((straightRow[col * 4 + 1] * tg + 127) / 255);
                    straightRow[col * 4 + 2] =
                        static_cast<std::uint8_t>((straightRow[col * 4 + 2] * tb + 127) / 255);
                    straightRow[col * 4 + 3] =
                        static_cast<std::uint8_t>((straightRow[col * 4 + 3] * ta + 127) / 255);
                }

                std::uint8_t* dstRow = dstBase + static_cast<std::ptrdiff_t>(row) * dstData.stride;
                ConvertStraightRgbaRowToPremultipliedBgra(straightRow.data(), dstRow, sw);
            }

            return tinted;
        }
    } // namespace

    void Blend2DSpriteBatchRenderer::Begin()
    {
        begun_ = true;
    }

    void Blend2DSpriteBatchRenderer::End()
    {
        begun_ = false;
    }

    void Blend2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        DrawQuad(texture, x, y, texture.GetWidth(), texture.GetHeight(), 0, 0, -1, -1,
                 Color(255, 255, 255, 255), 0.0, 0.0, 0.0, false, false);
    }

    void Blend2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        DrawQuad(texture, destinationRectangle.X, destinationRectangle.Y,
                 destinationRectangle.Width, destinationRectangle.Height, sourceRectangle.X,
                 sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height, color, 0.0, 0.0,
                 0.0, false, false);
    }

    void Blend2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color,
                                          float rotation, const Vector2& origin,
                                          SpriteEffects effects, float /*layerDepth*/)
    {
        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        DrawQuad(texture, destinationRectangle.X, destinationRectangle.Y,
                 destinationRectangle.Width, destinationRectangle.Height, sourceRectangle.X,
                 sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height, color,
                 static_cast<double>(rotation), origin.X, origin.Y, flipH, flipV);
    }

    void Blend2DSpriteBatchRenderer::DrawQuad(const ITextureRenderer& texture, double destX,
                                              double destY, double destW, double destH, int srcX,
                                              int srcY, int srcW, int srcH, const Color& color,
                                              double rotation, double originX, double originY,
                                              bool flipH, bool flipV)
    {
        if (!begun_)
            throw std::runtime_error("Blend2DSpriteBatchRenderer::Draw called before Begin().");

        const auto& source = dynamic_cast<const Blend2DNativeImageSourceEXT&>(texture);

        int sx = srcX, sy = srcY, sw = srcW, sh = srcH;
        if (sw <= 0 || sh <= 0)
        {
            sx = 0;
            sy = 0;
            sw = texture.GetWidth();
            sh = texture.GetHeight();
        }
        if (sw <= 0 || sh <= 0 || destW == 0.0 || destH == 0.0)
            return;

        const double scaleX = destW / static_cast<double>(sw);
        const double scaleY = destH / static_cast<double>(sh);
        const double originDestX = originX * scaleX;
        const double originDestY = originY * scaleY;

        const bool isWhite = color.getRProperty() == 255 && color.getGProperty() == 255 &&
                              color.getBProperty() == 255 && color.getAProperty() == 255;

        BLImage tinted;
        const BLImage* img = &source.NativeImageEXT();
        int blitSrcX = sx, blitSrcY = sy;
        if (!isWhite)
        {
            tinted = BuildTintedSubImageEXT(*img, sx, sy, sw, sh, color);
            img = &tinted;
            blitSrcX = 0;
            blitSrcY = 0;
        }

        BLContext& ctx = renderer_.ActiveSurface().Context();
        ctx.set_comp_op(renderer_.ActiveCompOp());
        ctx.save();
        ctx.translate(destX, destY);
        if (rotation != 0.0)
            ctx.rotate(rotation);
        if (flipH)
            ctx.scale(-1.0, 1.0);
        if (flipV)
            ctx.scale(1.0, -1.0);
        ctx.blit_image(BLRect(-originDestX, -originDestY, std::abs(destW), std::abs(destH)), *img,
                       BLRectI(blitSrcX, blitSrcY, sw, sh));
        ctx.restore();
    }
} // namespace CNA::Internal::Renderers::Blend2D
