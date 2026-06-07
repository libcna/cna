#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <cmath>
#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
        : backend_(graphicsDevice.GetBackend().CreateSpriteBatch()),
          graphicsDevice_(&graphicsDevice),
          begun(false)
    {
    }

    SpriteBatch::SpriteBatch()
        : begun(false)
    {
    }

    SpriteBatch::~SpriteBatch() = default;

    void SpriteBatch::Begin()
    {
        if (backend_)
        {
            backend_->Begin();
            begun = true;
        }
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
        (void)sprite_sort_mode;
        if (graphicsDevice_)
            graphicsDevice_->setBlendStateProperty(blend_state);
        Begin();
    }

    void SpriteBatch::End()
    {
        if (backend_)
        {
            backend_->End();
            begun = false;
        }
    }

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (backend_) backend_->Draw(texture.GetBackend(), x, y);
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>::value_type& value,
                           const Rectangle& destinationRectangle,
                           const Rectangle& sourceRectangle,
                           Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (backend_) backend_->Draw(value.GetBackend(), destinationRectangle, sourceRectangle, color);
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>& value,
                           const Rectangle& destinationRectangle,
                           const Rectangle& sourceRectangle,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (backend_ && value.has_value())
        {
            backend_->Draw(value->GetBackend(), destinationRectangle, sourceRectangle, color, rotation_rad, origin,
                           effect, layerDepth);
        }
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color)
    {
        DrawString(spriteFont, text, position, color, 0.0f, Vector2::Zero, Vector2(1.0f, 1.0f),
                   SpriteEffects::None, 0.0f);
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
        DrawString(spriteFont, text, position, color, rotation, origin, Vector2(scale, scale),
                   effects, layerDepth);
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

        const Texture2D* texture = spriteFont.textureValue_;
        if (texture == nullptr) return;

        const float sinR = std::sin(rotation);
        const float cosR = std::cos(rotation);

        Vector2 curOffset(0.0f, 0.0f);
        bool firstInLine = true;

        for (char raw : text)
        {
            const auto c = static_cast<charcs>(static_cast<unsigned char>(raw));

            if (c == u'\r')
            {
                continue;
            }
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
                {
                    throw std::invalid_argument(
                        "Text contains characters that cannot be resolved by this SpriteFont.");
                }
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

            // Glyph position in unscaled font space, relative to the rotation origin.
            const float localX = curOffset.X + static_cast<float>(cCrop.X) - origin.X;
            const float localY = curOffset.Y + static_cast<float>(cCrop.Y) - origin.Y;
            const float scaledX = localX * scale.X;
            const float scaledY = localY * scale.Y;
            const float rotX = scaledX * cosR - scaledY * sinR;
            const float rotY = scaledX * sinR + scaledY * cosR;

            const Rectangle dest(
                static_cast<intcs>(std::lround(position.X + rotX)),
                static_cast<intcs>(std::lround(position.Y + rotY)),
                static_cast<intcs>(std::lround(static_cast<float>(cGlyph.Width)  * scale.X)),
                static_cast<intcs>(std::lround(static_cast<float>(cGlyph.Height) * scale.Y)));

            if (rotation == 0.0f && effects == SpriteEffects::None)
            {
                backend_->Draw(texture->GetBackend(), dest, cGlyph, color);
            }
            else
            {
                backend_->Draw(texture->GetBackend(), dest, cGlyph, color, rotation,
                               Vector2::Zero, effects, layerDepth);
            }

            curOffset.X += cKern.Y + cKern.Z;
        }
    }
}
