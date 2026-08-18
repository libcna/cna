// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "CNA/Internal/Utf8Decode.hpp"
#include "System/ArgumentException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"

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

        // REMED-GFX-002: a defaultCharacter absent from characters would otherwise only be
        // caught later, at the point MeasureString/DrawString falls back to it -- reject the
        // inconsistency here instead, at the point it is actually introduced.
        if (defaultCharacter_.has_value() &&
            characterIndexMap_.find(defaultCharacter_.value()) == characterIndexMap_.end())
        {
            throw System::ArgumentException(
                "defaultCharacter is not present in characters.", "defaultCharacter");
        }
    }

    const std::vector<charcs>& SpriteFont::getCharactersProperty() const
    {
        return characterMap_;
    }

    const Texture2D& SpriteFont::getTextureEXT() const
    {
        return textureValue_;
    }

    const std::vector<Rectangle>& SpriteFont::getGlyphBoundsEXT() const
    {
        return glyphData_;
    }

    const std::vector<Rectangle>& SpriteFont::getCroppingEXT() const
    {
        return croppingData_;
    }

    const std::vector<Vector3>& SpriteFont::getKerningEXT() const
    {
        return kerning_;
    }

    std::optional<charcs> SpriteFont::getDefaultCharacterProperty() const
    {
        return defaultCharacter_;
    }

    void SpriteFont::setDefaultCharacterProperty(std::optional<charcs> value)
    {
        // REMED-GFX-002: same invariant as the constructor -- reject a fallback character that
        // this font cannot actually render, rather than letting MeasureString/DrawString hit an
        // unchecked end() dereference the first time the fallback is needed.
        if (value.has_value() && characterIndexMap_.find(value.value()) == characterIndexMap_.end())
        {
            throw System::ArgumentException(
                "defaultCharacter is not present in characters.", "value");
        }
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

        for (std::size_t i = 0; i < text.size();)
        {
            const charcs c = CNA::Internal::DecodeUtf8CodePoint(text, i);

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
                // REMED-GFX-002: defaultCharacter is validated on construction/set, so this
                // cannot fail in practice -- checked anyway rather than dereferencing end(),
                // matching FNA's characterIndexMap[DefaultCharacter.Value] Dictionary indexer,
                // which throws KeyNotFoundException on a miss.
                if (it == characterIndexMap_.end())
                {
                    throw System::Collections::Generic::KeyNotFoundException(
                        "defaultCharacter is not present in characters.");
                }
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

    Vector2 SpriteFont::MeasureString(const System::Text::StringBuilder& text) const
    {
        return MeasureString(text.ToString());
    }
}
