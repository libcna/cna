// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
        : backend_(graphicsDevice.GetBackend().CreateSpriteBatch()),
          graphicsDevice_(&graphicsDevice)
    {
    }

    SpriteBatch::SpriteBatch() = default;
    SpriteBatch::~SpriteBatch() = default;

    // -----------------------------------------------------------------------
    // Begin / End
    // -----------------------------------------------------------------------

    void SpriteBatch::Begin()
    {
        Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr,
              nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
        Begin(sprite_sort_mode, blend_state, nullptr, nullptr, nullptr, nullptr,
              Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState,
                            Effect* effect)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              effect, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            SamplerState* samplerState,
                            DepthStencilState* /*depthStencilState*/,
                            RasterizerState* /*rasterizerState*/,
                            Effect* effect,
                            Matrix transformMatrix)
    {
        if (begun)
            throw std::runtime_error("Begin has been called before calling End.");

        if (graphicsDevice_)
            graphicsDevice_->setBlendStateProperty(blendState);

        customEffect_    = effect;
        transformMatrix_ = transformMatrix;
        sortMode_        = sortMode;
        spriteQueue_.clear();
        begun            = true;

        if (backend_)
        {
            backend_->SetCustomEffect(customEffect_);
            backend_->SetTransformMatrix(transformMatrix_);
            if (samplerState)
                backend_->SetSamplerFilter(static_cast<int>(samplerState->getFilterProperty()));
            backend_->Begin();
        }
    }

    void SpriteBatch::End()
    {
        if (!begun)
            throw std::runtime_error("End was called, but Begin has not yet been called.");
        if (backend_)
        {
            if (sortMode_ != SpriteSortMode::Immediate)
                flushBatch();
            backend_->End();
            backend_->SetCustomEffect(nullptr);
        }
        begun         = false;
        customEffect_ = nullptr;
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    void SpriteBatch::pushSprite(const Texture2D& texture,
                                 const Rectangle& dest, const Rectangle& src,
                                 Color color, float rotation, Vector2 origin,
                                 SpriteEffects effects, float layerDepth)
    {
        SpriteInfo info;
        info.texture    = &texture;
        info.destRect   = dest;
        info.srcRect    = src;
        info.color      = color;
        info.rotation   = rotation;
        info.origin     = origin;
        info.effects    = effects;
        info.layerDepth = layerDepth;

        if (sortMode_ == SpriteSortMode::Immediate)
        {
            flushSingle(info);
        }
        else
        {
            spriteQueue_.push_back(info);
        }
    }

    void SpriteBatch::flushSingle(const SpriteInfo& s)
    {
        if (!backend_ || !s.texture) return;
        backend_->Draw(s.texture->GetBackend(),
                       s.destRect, s.srcRect, s.color,
                       s.rotation, s.origin, s.effects, s.layerDepth);
    }

    void SpriteBatch::flushBatch()
    {
        if (spriteQueue_.empty()) return;

        if (sortMode_ == SpriteSortMode::BackToFront)
        {
            std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(),
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return a.layerDepth > b.layerDepth;
                });
        }
        else if (sortMode_ == SpriteSortMode::FrontToBack)
        {
            std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(),
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return a.layerDepth < b.layerDepth;
                });
        }
        else if (sortMode_ == SpriteSortMode::Texture)
        {
            std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(),
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return a.texture < b.texture;
                });
        }
        // Deferred: no sort, submission order

        for (const SpriteInfo& s : spriteQueue_)
            flushSingle(s);

        spriteQueue_.clear();
    }

    // -----------------------------------------------------------------------
    // Draw overloads
    // -----------------------------------------------------------------------

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture,
                   Rectangle(static_cast<int>(x), static_cast<int>(y), w, h),
                   Rectangle(0, 0, w, h),
                   Color(255, 255, 255, 255),
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           const Rectangle& sourceRectangle,
                           Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        pushSprite(texture, destinationRectangle, sourceRectangle,
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           const Rectangle& sourceRectangle,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        pushSprite(texture, destinationRectangle, sourceRectangle,
                   color, rotation_rad, origin, effect, layerDepth);
    }

    // -----------------------------------------------------------------------
    // DrawString overloads
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // Draw stubs — XNA 4.0 overloads not yet fully implemented
    // -----------------------------------------------------------------------

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture,
                   Rectangle(static_cast<intcs>(position.X), static_cast<intcs>(position.Y), w, h),
                   Rectangle(0, 0, w, h),
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        pushSprite(texture,
                   Rectangle(static_cast<intcs>(position.X), static_cast<intcs>(position.Y), dw, dh),
                   src, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color,
                           float rotation, Vector2 origin, float scale,
                           SpriteEffects effects, float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        pushSprite(texture,
                   Rectangle(static_cast<intcs>(position.X), static_cast<intcs>(position.Y),
                             static_cast<intcs>(dw * scale), static_cast<intcs>(dh * scale)),
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color,
                           float rotation, Vector2 origin, Vector2 scale,
                           SpriteEffects effects, float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        pushSprite(texture,
                   Rectangle(static_cast<intcs>(position.X), static_cast<intcs>(position.Y),
                             static_cast<intcs>(dw * scale.X), static_cast<intcs>(dh * scale.Y)),
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture, destinationRectangle, Rectangle(0, 0, w, h),
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           std::optional<Rectangle> sourceRectangle, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!backend_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        pushSprite(texture, destinationRectangle, src,
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    // -----------------------------------------------------------------------
    // DrawString overloads
    // -----------------------------------------------------------------------

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color)
    {
        DrawString(spriteFont, text, position, color, 0.0f, Vector2::Zero,
                   Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color,
                                 float rotation,
                                 Vector2 origin,
                                 float scale,
                                 SpriteEffects effects,
                                 float layerDepth)
    {
        DrawString(spriteFont, text, position, color, rotation, origin,
                   Vector2(scale, scale), effects, layerDepth);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color,
                                 float rotation,
                                 Vector2 origin,
                                 Vector2 scale,
                                 SpriteEffects effects,
                                 float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::DrawString called before Begin().");
        if (!backend_ || text.empty()) return;

        const Texture2D& texture = spriteFont.textureValue_;
        if (texture.getWidthProperty() == 0) return;

        const float sinR = std::sin(rotation);
        const float cosR = std::cos(rotation);

        Vector2 curOffset(0.0f, 0.0f);
        bool firstInLine = true;

        for (char raw : text)
        {
            const auto c = static_cast<charcs>(static_cast<unsigned char>(raw));

            if (c == u'\r') continue;
            if (c == u'\n')
            {
                curOffset.X = 0.0f;
                curOffset.Y += static_cast<float>(spriteFont.lineSpacing_);
                firstInLine = true;
                continue;
            }

            auto it = spriteFont.characterIndexMap_.find(c);
            if (it == spriteFont.characterIndexMap_.end())
            {
                if (!spriteFont.defaultCharacter_.has_value())
                    throw std::invalid_argument(
                        "Text contains characters that cannot be resolved by this SpriteFont.");
                it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value());
            }
            const int index = it->second;

            const Vector3& cKern = spriteFont.kerning_[index];
            if (firstInLine)
            {
                curOffset.X += std::abs(cKern.X);
                firstInLine = false;
            }
            else
            {
                curOffset.X += spriteFont.spacing_ + cKern.X;
            }

            const Rectangle& cCrop  = spriteFont.croppingData_[index];
            const Rectangle& cGlyph = spriteFont.glyphData_[index];

            const float localX  = curOffset.X + static_cast<float>(cCrop.X) - origin.X;
            const float localY  = curOffset.Y + static_cast<float>(cCrop.Y) - origin.Y;
            const float scaledX = localX * scale.X;
            const float scaledY = localY * scale.Y;
            const float rotX    = scaledX * cosR - scaledY * sinR;
            const float rotY    = scaledX * sinR + scaledY * cosR;

            const Rectangle dest(
                static_cast<intcs>(std::lround(position.X + rotX)),
                static_cast<intcs>(std::lround(position.Y + rotY)),
                static_cast<intcs>(std::lround(static_cast<float>(cGlyph.Width)  * scale.X)),
                static_cast<intcs>(std::lround(static_cast<float>(cGlyph.Height) * scale.Y)));

            pushSprite(texture, dest, cGlyph, color,
                       rotation, Vector2::Zero, effects, layerDepth);

            curOffset.X += cKern.Y + cKern.Z;
        }
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color)
    {
        DrawString(spriteFont, text.ToString(), position, color);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color,
                                 float rotation, Vector2 origin, float scale,
                                 SpriteEffects effects, float layerDepth)
    {
        DrawString(spriteFont, text.ToString(), position, color,
                   rotation, origin, scale, effects, layerDepth);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color,
                                 float rotation, Vector2 origin, Vector2 scale,
                                 SpriteEffects effects, float layerDepth)
    {
        DrawString(spriteFont, text.ToString(), position, color,
                   rotation, origin, scale, effects, layerDepth);
    }
}
