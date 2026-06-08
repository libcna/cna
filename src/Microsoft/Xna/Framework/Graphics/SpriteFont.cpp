#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

#include <algorithm>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    SpriteFont::SpriteFont(Texture2D texture,
                           std::vector<Rectangle> glyphBounds,
                           std::vector<Rectangle> cropping,
                           std::vector<charcs> characters,
                           int lineSpacing,
                           float spacing,
                           std::vector<Vector3> kerningData,
                           std::optional<charcs> defaultCharacter)
        : textureValue_(std::move(texture))
        , glyphData_(std::move(glyphBounds))
        , croppingData_(std::move(cropping))
        , kerning_(std::move(kerningData))
        , characterMap_(std::move(characters))
        , defaultCharacter_(defaultCharacter)
        , lineSpacing_(lineSpacing)
        , spacing_(spacing)
    {
        characterIndexMap_.reserve(characterMap_.size());
        for (int i = 0; i < static_cast<int>(characterMap_.size()); ++i)
        {
            characterIndexMap_[characterMap_[i]] = i;
        }
    }

    const std::vector<charcs>& SpriteFont::getCharactersProperty() const
    {
        return characterMap_;
    }

    std::optional<charcs> SpriteFont::getDefaultCharacterProperty() const
    {
        return defaultCharacter_;
    }

    void SpriteFont::setDefaultCharacterProperty(std::optional<charcs> value)
    {
        defaultCharacter_ = value;
    }

    int SpriteFont::getLineSpacingProperty() const
    {
        return lineSpacing_;
    }

    void SpriteFont::setLineSpacingProperty(int value)
    {
        lineSpacing_ = value;
    }

    float SpriteFont::getSpacingProperty() const
    {
        return spacing_;
    }

    void SpriteFont::setSpacingProperty(float value)
    {
        spacing_ = value;
    }

    Vector2 SpriteFont::MeasureString(const String& text) const
    {
        if (text.empty())
        {
            return Vector2::Zero;
        }

        Vector2 result = Vector2::Zero;
        float curLineWidth = 0.0f;
        float finalLineHeight = static_cast<float>(lineSpacing_);
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
                result.X = std::max(result.X, curLineWidth);
                result.Y += static_cast<float>(lineSpacing_);
                curLineWidth = 0.0f;
                finalLineHeight = static_cast<float>(lineSpacing_);
                firstInLine = true;
                continue;
            }

            auto it = characterIndexMap_.find(c);
            if (it == characterIndexMap_.end())
            {
                if (!defaultCharacter_.has_value())
                {
                    throw std::invalid_argument(
                        "Text contains characters that cannot be resolved by this SpriteFont.");
                }
                it = characterIndexMap_.find(defaultCharacter_.value());
            }
            const int index = it->second;

            const Vector3& cKern = kerning_[index];
            if (firstInLine)
            {
                curLineWidth += std::abs(cKern.X);
                firstInLine = false;
            }
            else
            {
                curLineWidth += spacing_ + cKern.X;
            }

            curLineWidth += cKern.Y + cKern.Z;

            const int cCropHeight = croppingData_[index].Height;
            if (static_cast<float>(cCropHeight) > finalLineHeight)
            {
                finalLineHeight = static_cast<float>(cCropHeight);
            }
        }

        result.X = std::max(result.X, curLineWidth);
        result.Y += finalLineHeight;

        return result;
    }
}
