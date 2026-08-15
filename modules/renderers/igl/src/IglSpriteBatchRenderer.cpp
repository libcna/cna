// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        /// Beyond this many queued quads the batch is flushed on its own, so a very long
        /// Begin/End block does not grow an unbounded staging vector before it reaches the GPU.
        /// 16-bit indices also cap a single draw at 65535 vertices, which is 16383 quads.
        constexpr std::size_t kMaxQueuedQuads = 8192;
    }

    IglSpriteBatchRenderer::IglSpriteBatchRenderer(IglRenderer& owner)
        : owner_(owner), transform_(Matrix::getIdentityProperty())
    {
    }

    void IglSpriteBatchRenderer::Begin()
    {
        Flush();
        inBlock_ = true;
        transform_ = Matrix::getIdentityProperty();
        batchTexture_ = nullptr;
    }

    void IglSpriteBatchRenderer::End()
    {
        Flush();
        inBlock_ = false;
        customEffect_ = nullptr;
    }

    void IglSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        // The transform is part of the batch's identity: sprites already queued were positioned for
        // the previous one, so they must reach the GPU before it changes.
        Flush();
        transform_ = m;
    }

    void IglSpriteBatchRenderer::SetSamplerFilter(const int textureFilter)
    {
        if (textureFilter == textureFilter_)
            return;
        Flush();
        textureFilter_ = textureFilter;
    }

    void IglSpriteBatchRenderer::SetSamplerAddressMode(const int addressU, const int addressV)
    {
        if (addressU == addressU_ && addressV == addressV_)
            return;
        Flush();
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void IglSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect == customEffect_)
            return;
        Flush();
        customEffect_ = effect;
        if (customEffect_ != nullptr)
            customEffect_->Apply();
    }

    void IglSpriteBatchRenderer::SetImmediateMode(const bool immediate)
    {
        immediate_ = immediate;
    }

    void IglSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const float x, const float y)
    {
        const Rectangle destination(static_cast<int>(std::lround(x)),
                                    static_cast<int>(std::lround(y)),
                                    texture.GetWidth(), texture.GetHeight());
        const Rectangle source(0, 0, texture.GetWidth(), texture.GetHeight());
        Draw(texture, destination, source, Color(static_cast<bytecs>(255), static_cast<bytecs>(255), static_cast<bytecs>(255), static_cast<bytecs>(255)), 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void IglSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void IglSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color,
                                      const float rotation, const Vector2& origin,
                                      const SpriteEffects effects, const float layerDepth)
    {
        if (batchTexture_ != nullptr && batchTexture_ != &texture)
            Flush();
        batchTexture_ = &texture;

        QueueQuad(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                  layerDepth);

        // Immediate mode means device state changed between two Draw() calls must be honoured per
        // sprite, so the batch cannot outlive a single quad.
        if (immediate_ || vertices_.size() >= kMaxQueuedQuads * 4)
            Flush();
    }

    void IglSpriteBatchRenderer::QueueQuad(const ITextureRenderer& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle, const Color& color,
                                           const float rotation, const Vector2& origin,
                                           const SpriteEffects effects, const float layerDepth)
    {
        const float textureWidth = static_cast<float>(texture.GetWidth());
        const float textureHeight = static_cast<float>(texture.GetHeight());
        if (textureWidth <= 0.0f || textureHeight <= 0.0f)
            return;

        float u0 = static_cast<float>(sourceRectangle.X) / textureWidth;
        float v0 = static_cast<float>(sourceRectangle.Y) / textureHeight;
        float u1 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / textureWidth;
        float v1 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / textureHeight;

        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
            std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
            std::swap(v0, v1);

        // XNA's origin is expressed in source texels, so it scales with the destination rectangle
        // exactly as the sprite itself does.
        const float scaleX = sourceRectangle.Width != 0
                                 ? static_cast<float>(destinationRectangle.Width) /
                                       static_cast<float>(sourceRectangle.Width)
                                 : 1.0f;
        const float scaleY = sourceRectangle.Height != 0
                                 ? static_cast<float>(destinationRectangle.Height) /
                                       static_cast<float>(sourceRectangle.Height)
                                 : 1.0f;
        const float originX = origin.X * scaleX;
        const float originY = origin.Y * scaleY;

        const float left = -originX;
        const float top = -originY;
        const float right = left + static_cast<float>(destinationRectangle.Width);
        const float bottom = top + static_cast<float>(destinationRectangle.Height);

        const float sin = std::sin(rotation);
        const float cos = std::cos(rotation);
        const float anchorX = static_cast<float>(destinationRectangle.X);
        const float anchorY = static_cast<float>(destinationRectangle.Y);

        const auto transformCorner = [&](const float localX, const float localY, float& outX,
                                         float& outY) {
            outX = anchorX + localX * cos - localY * sin;
            outY = anchorY + localX * sin + localY * cos;
        };

        const std::uint16_t base = static_cast<std::uint16_t>(vertices_.size());

        IglSpriteVertex corners[4];
        transformCorner(left, top, corners[0].position[0], corners[0].position[1]);
        transformCorner(right, top, corners[1].position[0], corners[1].position[1]);
        transformCorner(right, bottom, corners[2].position[0], corners[2].position[1]);
        transformCorner(left, bottom, corners[3].position[0], corners[3].position[1]);

        corners[0].texCoord[0] = u0; corners[0].texCoord[1] = v0;
        corners[1].texCoord[0] = u1; corners[1].texCoord[1] = v0;
        corners[2].texCoord[0] = u1; corners[2].texCoord[1] = v1;
        corners[3].texCoord[0] = u0; corners[3].texCoord[1] = v1;

        for (IglSpriteVertex& vertex : corners)
        {
            vertex.position[2] = layerDepth;
            vertex.color[0] = color.getRProperty();
            vertex.color[1] = color.getGProperty();
            vertex.color[2] = color.getBProperty();
            vertex.color[3] = color.getAProperty();
            vertices_.push_back(vertex);
        }

        indices_.push_back(static_cast<std::uint16_t>(base + 0));
        indices_.push_back(static_cast<std::uint16_t>(base + 1));
        indices_.push_back(static_cast<std::uint16_t>(base + 2));
        indices_.push_back(static_cast<std::uint16_t>(base + 0));
        indices_.push_back(static_cast<std::uint16_t>(base + 2));
        indices_.push_back(static_cast<std::uint16_t>(base + 3));
    }

    void IglSpriteBatchRenderer::Flush()
    {
        if (vertices_.empty() || indices_.empty())
        {
            vertices_.clear();
            indices_.clear();
            return;
        }

        owner_.DrawSpriteBatchEXT(vertices_, indices_, batchTexture_, textureFilter_, addressU_,
                                  addressV_, transform_, customEffect_);

        vertices_.clear();
        indices_.clear();
        batchTexture_ = nullptr;
    }
}
