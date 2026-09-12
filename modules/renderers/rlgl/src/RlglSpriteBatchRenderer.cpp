// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector2;
        using Microsoft::Xna::Framework::Graphics::Effect;
        using Microsoft::Xna::Framework::Graphics::SpriteEffects;

        constexpr int kFloatsPerVertex = 8;
        constexpr int kMaximumSpritesPerGroup = 16383;
        constexpr int kVertexCapacity = kMaximumSpritesPerGroup * 4;
        constexpr int kIndexCapacity = kMaximumSpritesPerGroup * 6;

        class RlglSpriteBatchRenderer final : public ISpriteBatchRenderer
        {
        public:
            explicit RlglSpriteBatchRenderer(RlglRenderer& renderer)
                : renderer_(renderer)
                , pipeline_(Bridge::CreateSpritePipeline(
                      kVertexCapacity, kIndexCapacity))
            {
                vertices_.reserve(
                    static_cast<std::size_t>(kVertexCapacity) * kFloatsPerVertex);
                indices_.reserve(kIndexCapacity);
            }

            ~RlglSpriteBatchRenderer() override
            {
                Bridge::DestroySpritePipeline(pipeline_);
            }

            RlglSpriteBatchRenderer(const RlglSpriteBatchRenderer&) = delete;
            RlglSpriteBatchRenderer& operator=(const RlglSpriteBatchRenderer&) = delete;

            void Begin() override
            {
                if (begun_)
                    throw std::runtime_error("RLGL: SpriteBatch.Begin called twice");
                begun_ = true;
            }

            void End() override
            {
                if (!begun_)
                    throw std::runtime_error("RLGL: SpriteBatch.End called before Begin");
                Flush();
                begun_ = false;
            }

            void SetTransformMatrix(const Matrix& matrix) override
            {
                transform_ = matrix;
            }

            void SetCustomEffect(Effect* effect) override
            {
                if (effect != nullptr)
                {
                    throw System::NotSupportedException(
                        "RLGL: custom SpriteBatch Effect is not implemented yet "
                        "(plans/plan_rlgl.md RLGL-012)");
                }
            }

            void SetSamplerState(
                const int textureFilter, const int addressU, const int addressV,
                const int addressW, const int maxAnisotropy,
                const int maxMipLevel, const float lodBias) override
            {
                filter_ = textureFilter;
                addressU_ = addressU;
                addressV_ = addressV;
                addressW_ = addressW;
                maxAnisotropy_ = maxAnisotropy;
                maxMipLevel_ = maxMipLevel;
                lodBias_ = lodBias;
            }

            void SetImmediateMode(const bool immediate) override
            {
                immediate_ = immediate;
            }

            void Draw(const ITextureRenderer& texture, const float x, const float y) override
            {
                const int width = texture.GetWidth();
                const int height = texture.GetHeight();
                Draw(texture, x, y, static_cast<float>(width), static_cast<float>(height),
                     Rectangle(0, 0, width, height), Color::White, 0.0f,
                     Vector2::Zero, SpriteEffects::None, 0.0f);
            }

            void Draw(
                const ITextureRenderer& texture,
                const Rectangle& destinationRectangle,
                const Rectangle& sourceRectangle,
                const Color& color) override
            {
                Draw(texture,
                     static_cast<float>(destinationRectangle.X),
                     static_cast<float>(destinationRectangle.Y),
                     static_cast<float>(destinationRectangle.Width),
                     static_cast<float>(destinationRectangle.Height),
                     sourceRectangle, color, 0.0f, Vector2::Zero,
                     SpriteEffects::None, 0.0f);
            }

            void Draw(
                const ITextureRenderer& texture,
                const Rectangle& destinationRectangle,
                const Rectangle& sourceRectangle,
                const Color& color, const float rotation,
                const Vector2& origin, const SpriteEffects effects,
                const float layerDepth) override
            {
                Draw(texture,
                     static_cast<float>(destinationRectangle.X),
                     static_cast<float>(destinationRectangle.Y),
                     static_cast<float>(destinationRectangle.Width),
                     static_cast<float>(destinationRectangle.Height),
                     sourceRectangle, color, rotation, origin, effects, layerDepth);
            }

            void Draw(
                const ITextureRenderer& texture,
                const float destinationX, const float destinationY,
                const float destinationWidth, const float destinationHeight,
                const Rectangle& sourceRectangle,
                const Color& color, const float rotation,
                const Vector2& origin, const SpriteEffects effects,
                const float layerDepth) override
            {
                (void)layerDepth;
                if (!begun_)
                    throw std::runtime_error("RLGL: SpriteBatch.Draw called before Begin");

                if (currentTexture_ != &texture || VertexCount() + 4 > kVertexCapacity)
                {
                    Flush();
                    currentTexture_ = &texture;
                }

                const float textureWidth = static_cast<float>(texture.GetWidth());
                const float textureHeight = static_cast<float>(texture.GetHeight());
                float u1 = static_cast<float>(sourceRectangle.X) / textureWidth;
                float v1 = static_cast<float>(sourceRectangle.Y) / textureHeight;
                float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) /
                    textureWidth;
                float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) /
                    textureHeight;
                if ((static_cast<int>(effects) &
                     static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
                {
                    std::swap(u1, u2);
                }
                if ((static_cast<int>(effects) &
                     static_cast<int>(SpriteEffects::FlipVertically)) != 0)
                {
                    std::swap(v1, v2);
                }
                if (SampledRowsAreBottomUp(texture))
                {
                    v1 = 1.0f - v1;
                    v2 = 1.0f - v2;
                }

                const float sourceWidth = static_cast<float>(sourceRectangle.Width);
                const float sourceHeight = static_cast<float>(sourceRectangle.Height);
                const float scaleX = destinationWidth / sourceWidth;
                const float scaleY = destinationHeight / sourceHeight;
                const float left = -origin.X * scaleX;
                const float top = -origin.Y * scaleY;
                const float right = (sourceWidth - origin.X) * scaleX;
                const float bottom = (sourceHeight - origin.Y) * scaleY;
                const float cosine = std::cos(rotation);
                const float sine = std::sin(rotation);

                const auto position = [&](const float x, const float y)
                {
                    return Vector2(
                        destinationX + x * cosine - y * sine,
                        destinationY + x * sine + y * cosine);
                };
                const Vector2 p0 = position(left, top);
                const Vector2 p1 = position(right, top);
                const Vector2 p2 = position(right, bottom);
                const Vector2 p3 = position(left, bottom);

                const float red = static_cast<float>(color.getRProperty()) / 255.0f;
                const float green = static_cast<float>(color.getGProperty()) / 255.0f;
                const float blue = static_cast<float>(color.getBProperty()) / 255.0f;
                const float alpha = static_cast<float>(color.getAProperty()) / 255.0f;
                const auto appendVertex = [&](const Vector2& p, const float u, const float v)
                {
                    vertices_.insert(vertices_.end(),
                        {p.X, p.Y, u, v, red, green, blue, alpha});
                };

                const auto base = static_cast<std::uint16_t>(VertexCount());
                appendVertex(p0, u1, v1);
                appendVertex(p1, u2, v1);
                appendVertex(p2, u2, v2);
                appendVertex(p3, u1, v2);
                indices_.insert(indices_.end(), {
                    static_cast<std::uint16_t>(base + 0),
                    static_cast<std::uint16_t>(base + 1),
                    static_cast<std::uint16_t>(base + 2),
                    static_cast<std::uint16_t>(base + 2),
                    static_cast<std::uint16_t>(base + 3),
                    static_cast<std::uint16_t>(base + 0)});

                if (immediate_) Flush();
            }

        private:
            [[nodiscard]] int VertexCount() const
            {
                return static_cast<int>(vertices_.size() / kFloatsPerVertex);
            }

            void Flush()
            {
                if (vertices_.empty()) return;
                if (currentTexture_ == nullptr)
                    throw std::runtime_error("RLGL: SpriteBatch lost its texture group");

                int viewportWidth = 0;
                int viewportHeight = 0;
                renderer_.GetSpriteBatchViewportSize(viewportWidth, viewportHeight);
                if (viewportWidth <= 0 || viewportHeight <= 0)
                    throw std::runtime_error("RLGL: SpriteBatch viewport is empty");

                const Matrix orthographic = Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(viewportWidth),
                    static_cast<float>(viewportHeight), 0.0f, -1.0f, 1.0f);
                const Matrix combined = transform_ * orthographic;
                float projection[16] = {};
                combined.ToColumnMajor(projection);

                // The production path never records into rlgl's global immediate batch. Drain
                // any diagnostic work before establishing CNA's texture/sampler bindings.
                Bridge::FlushImmediateBatch();
                currentTexture_->BindGL(0);
                renderer_.ApplySamplerState(
                    0, filter_, addressU_, addressV_, maxAnisotropy_);
                renderer_.ApplySamplerMipState(0, maxMipLevel_, lodBias_);
                renderer_.ApplySamplerAddressW(0, addressW_);
                Bridge::DrawSpriteGeometry(
                    pipeline_, vertices_.data(), VertexCount(),
                    indices_.data(), static_cast<int>(indices_.size()), projection);

                vertices_.clear();
                indices_.clear();
                currentTexture_ = nullptr;
            }

            RlglRenderer& renderer_;
            Bridge::SpritePipeline pipeline_{};
            std::vector<float> vertices_;
            std::vector<std::uint16_t> indices_;
            const ITextureRenderer* currentTexture_ = nullptr;
            Matrix transform_ = Matrix::getIdentityProperty();
            int filter_ = 0;
            int addressU_ = 1;
            int addressV_ = 1;
            int addressW_ = 1;
            int maxAnisotropy_ = 4;
            int maxMipLevel_ = 0;
            float lodBias_ = 0.0f;
            bool immediate_ = false;
            bool begun_ = false;
        };
    }

    std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatchRenderer(
        RlglRenderer& renderer)
    {
        return std::make_unique<RlglSpriteBatchRenderer>(renderer);
    }
}
