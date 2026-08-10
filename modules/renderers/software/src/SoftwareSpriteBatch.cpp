// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Renderers::Software
{
    using Vector3 = Microsoft::Xna::Framework::Vector3;

    // Phase S6 (SOFTWARE-51): SpriteBatch owns the public quad geometry and transform path; its
    // four prepared corners then enter SoftwareRenderer::RasterizeSpriteQuad(), where they
    // reuse the same shared CPU triangle fragment rasterizer as the complete Software 3D renderer.

    SoftwareSpriteBatchRenderer::SoftwareSpriteBatchRenderer(SoftwareRenderer& owner) : owner_(owner) {}

    void SoftwareSpriteBatchRenderer::Begin()
    {
        if (begun_)
            throw std::runtime_error("SoftwareSpriteBatchRenderer::Begin: Begin() called without a matching End()");
        begun_ = true;
    }

    void SoftwareSpriteBatchRenderer::End()
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchRenderer::End: End() called without a matching Begin()");
        begun_ = false;
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchRenderer::Draw: Draw() called before Begin()");

        const float texW = static_cast<float>(std::max(1, texture.GetWidth()));
        const float texH = static_cast<float>(std::max(1, texture.GetHeight()));
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

        const float r = color.getRProperty() / 255.0f;
        const float g = color.getGProperty() / 255.0f;
        const float b = color.getBProperty() / 255.0f;
        const float a = color.getAProperty() / 255.0f;

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);
        const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
        const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
        const float ox = origin.X;
        const float oy = origin.Y;
        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX, p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX, p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        // REMED-GFX-073: SpriteBatch coordinates are VIEWPORT-LOCAL. The Software path builds the
        // viewport-local corner, applies the optional transform, then positions it at the viewport
        // origin (which is deliberately not itself transformed).
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        owner_.GetActiveViewport(vpX, vpY, vpW, vpH);
        const auto placeCorner = [&](float px, float py) -> Vector2 {
            const float rx = dx + px * cosR - py * sinR;
            const float ry = dy + px * sinR + py * cosR;
            const Vector3 transformed = Vector3::Transform(Vector3(rx, ry, 0.0f), transformMatrix_);
            return Vector2(transformed.X + static_cast<float>(vpX),
                           transformed.Y + static_cast<float>(vpY));
        };

        const Vector2 c0 = placeCorner(p0x, p0y);
        const Vector2 c1 = placeCorner(p1x, p1y);
        const Vector2 c2 = placeCorner(p2x, p2y);
        const Vector2 c3 = placeCorner(p3x, p3y);

        // A non-finite transform cannot cover a defined framebuffer pixel. Reject it before both
        // damage calculation and raster edge math, keeping huge/invalid matrices deterministic.
        if (!std::isfinite(c0.X) || !std::isfinite(c0.Y) ||
            !std::isfinite(c1.X) || !std::isfinite(c1.Y) ||
            !std::isfinite(c2.X) || !std::isfinite(c2.Y) ||
            !std::isfinite(c3.X) || !std::isfinite(c3.Y))
            return;

        owner_.RasterizeSpriteQuad(texture, c0, c1, c2, c3, layerDepth, r, g, b, a, u1, v1, u2, v2,
                                   customEffect_, GetSamplerState());
    }
}
