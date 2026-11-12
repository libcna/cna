// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include <bit>
#include <cstdint>

#include "System/Char.hpp"
#include <algorithm>
#include <cstddef>
#include "CNA/Internal/Utf8Decode.hpp"
#include <utility>
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        std::string CharacterNotInFont(charcs character)
        {
            // XNA 4.0 Refresh set_DefaultCharacter IL_001d-0057 and
            // GetIndexForCharacter IL_006a-009c use FrameworkResources.CharacterNotInFont.
            constexpr char hex[] = "0123456789abcdef";
            const auto code = static_cast<unsigned int>(character);
            std::string digits(4, '0');
            for (unsigned int i = 0; i < 4; ++i)
                digits[3 - i] = hex[(code >> (4 * i)) & 15];
            // XNA formats the missing UTF-16 unit itself; a lone surrogate (half of a supplementary
            // character) has no UTF-8 form, so the display character becomes U+FFFD while the hex
            // still names the unit.
            const bool surrogate = code >= 0xD800u && code <= 0xDFFFu;
            return "The character '" +
                (surrogate ? std::string("\xEF\xBF\xBD") : System::Char::ToString(character)) +
                "' (0x" + digits +
                ") is not available in this SpriteFont. If applicable, adjust the font's "
                "start and end CharacterRegions to include this character.";
        }
    }

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
        // Unlike the internal content constructor, Microsoft's public setter validates the new
        // fallback before changing the property.
        if (value.has_value() && characterIndexMap_.find(value.value()) == characterIndexMap_.end())
        {
            throw System::ArgumentException(CharacterNotInFont(value.value()));
        }
        defaultCharacter_ = value;
    }

    int SpriteFont::getIndexForCharacter(charcs character) const
    {
        const auto found = characterIndexMap_.find(character);
        if (found != characterIndexMap_.end())
        {
            return found->second;
        }
        if (defaultCharacter_.has_value() && character != defaultCharacter_.value())
        {
            return getIndexForCharacter(defaultCharacter_.value());
        }
        throw System::ArgumentException(
            CharacterNotInFont(character), "character");
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
        std::uint32_t lineBreakCount = 0;
        // Refresh InternalMeasure IL_0054-0068/015c-0170 clamps only a line's final C.
        // IL_00da-0108 groups (Spacing + previous C) and (A + B) separately in Single.
        float pendingRightBearing = 0.0f;

        // One UTF-16 code unit at a time, as XNA's StringProxy walks a .NET string: a supplementary
        // character is its high surrogate, then its low one.
        charcs pendingLowSurrogate = 0;
        for (std::size_t i = 0; i < text.size() || pendingLowSurrogate != 0;)
        {
            const charcs c = pendingLowSurrogate != 0
                ? std::exchange(pendingLowSurrogate, charcs{0})
                : CNA::Internal::DecodeUtf8CodePoint(text, i, &pendingLowSurrogate);

            if (c == u'\r')
            {
                continue;
            }
            if (c == u'\n')
            {
                result.X = std::max(
                    result.X, curLineWidth + std::max(pendingRightBearing, 0.0f));
                ++lineBreakCount;
                curLineWidth = 0.0f;
                pendingRightBearing = 0.0f;
                finalLineHeight = static_cast<float>(lineSpacing_);
                firstInLine = true;
                continue;
            }

            const int index = getIndexForCharacter(c);

            const Vector3& cKern = kerning_[index];
            const float leftBearing = firstInLine ? std::max(cKern.X, 0.0f) : cKern.X;
            if (!firstInLine)
            {
                // Refresh IL_00e8-00eb stores this sum before adding the glyph's A+B.
                curLineWidth += spacing_ + pendingRightBearing;
            }
            curLineWidth += leftBearing + cKern.Y;
            firstInLine = false;
            pendingRightBearing = cKern.Z;

            const int cCropHeight = croppingData_[index].Height;
            if (static_cast<float>(cCropHeight) > finalLineHeight)
            {
                finalLineHeight = static_cast<float>(cCropHeight);
            }
        }

        result.X = std::max(result.X, curLineWidth + std::max(pendingRightBearing, 0.0f));
        // Refresh IL_017d-0187 multiplies unchecked Int32 before conv.r4, then adds.
        // Unsigned multiplication plus bit_cast preserves that wrap without signed C++ overflow.
        const auto lineProduct = std::bit_cast<std::int32_t>(
            lineBreakCount * static_cast<std::uint32_t>(lineSpacing_));
        result.Y = finalLineHeight + static_cast<float>(lineProduct);

        return result;
    }

    Vector2 SpriteFont::MeasureString(const System::Text::StringBuilder& text) const
    {
        return MeasureString(text.ToString());
    }
}
