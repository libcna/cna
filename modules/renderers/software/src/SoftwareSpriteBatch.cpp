// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Renderers::Software
{
    using Vector3 = Microsoft::Xna::Framework::Vector3;
    using Vector4 = Microsoft::Xna::Framework::Vector4;

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
        Draw(texture, x, y, static_cast<float>(texture.GetWidth()),
             static_cast<float>(texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()),
             Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
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
        Draw(texture,
             static_cast<float>(destinationRectangle.X),
             static_cast<float>(destinationRectangle.Y),
             static_cast<float>(destinationRectangle.Width),
             static_cast<float>(destinationRectangle.Height),
             sourceRectangle, color, rotation, origin, effects, layerDepth);
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          float destinationX,
                                          float destinationY,
                                          float destinationWidth,
                                          float destinationHeight,
                                          const Rectangle& sourceRectangle,
                                          const Color& color,
                                          float rotation,
                                          const Vector2& origin,
                                          SpriteEffects effects,
                                          float layerDepth)
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

        const float dx = destinationX;
        const float dy = destinationY;
        const float dw = destinationWidth;
        const float dh = destinationHeight;
        // Source extents remain signed through XNA's sprite calculation. Their sign participates
        // in the origin transform, and a zero extent must not be expanded into one texel.
        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);
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

        // SOFTWARE-338: preserve FNA's complete `(x,y,layerDepth,1) * transformMatrix` result.
        // RasterizeSpriteQuad applies the SpriteBatch projection and viewport after homogeneous
        // clipping, so Viewport.X/Y remain outside the caller's transform (REMED-GFX-073).
        const auto placeCorner = [&](float px, float py) -> Vector4 {
            const float rx = dx + px * cosR - py * sinR;
            const float ry = dy + px * sinR + py * cosR;
            return Vector4::Transform(Vector3(rx, ry, layerDepth), transformMatrix_);
        };

        const Vector4 c0 = placeCorner(p0x, p0y);
        const Vector4 c1 = placeCorner(p1x, p1y);
        const Vector4 c2 = placeCorner(p2x, p2y);
        const Vector4 c3 = placeCorner(p3x, p3y);

        // A non-finite transform cannot cover a defined framebuffer pixel. Reject it before both
        // damage calculation and raster edge math, keeping huge/invalid matrices deterministic.
        const auto finite = [](const Vector4& value) {
            return std::isfinite(value.X) && std::isfinite(value.Y) &&
                   std::isfinite(value.Z) && std::isfinite(value.W);
        };
        if (!finite(c0) || !finite(c1) || !finite(c2) || !finite(c3))
            return;

        owner_.RasterizeSpriteQuad(texture, c0, c1, c2, c3, r, g, b, a, u1, v1, u2, v2,
                                   customEffect_, GetSamplerState());
    }
}
