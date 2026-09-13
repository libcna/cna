// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

#include <algorithm>
#include <cstddef>
#include "CNA/Internal/Utf8Decode.hpp"
#include "System/ArgumentException.hpp"

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
            throw System::ArgumentException("defaultCharacter is not present in characters.");
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
            "Character cannot be resolved by this SpriteFont.", "character");
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
        // The previous glyph's RIGHT side bearing, held rather than added.
        //
        // XNA keeps it in a local, adds it to the next glyph unclamped (IL_00da, IL_010d) and
        // adds Math.Max(pendingZ, 0) once per line break and once after the loop (IL_0054,
        // IL_015c). So a line's last glyph contributes its right bearing only when that bearing
        // is positive: a negative one is an overhang, which occupies no width to the right of
        // where the line ends.
        //
        // FNA/src/Graphics/SpriteFont.cs:201 writes `curLineWidth += cKern.Y + cKern.Z` instead,
        // in both of its measure paths, which is right for every interior glyph and wrong for the
        // last one on a line. The result was short by the overhang whenever the widest line ended
        // in a glyph with a negative right bearing -- silently, font-dependently, and growing with
        // the number of lines. CLAUDE.md settles the direction: XNA wins. The first-glyph left
        // bearing above already diverges from FNA the same way, measured against a live XNA build.
        float pendingRightBearing = 0.0f;

        for (std::size_t i = 0; i < text.size();)
        {
            const charcs c = CNA::Internal::DecodeUtf8CodePoint(text, i);

            if (c == u'\r')
            {
                continue;
            }
            if (c == u'\n')
            {
                result.X = std::max(
                    result.X, curLineWidth + std::max(pendingRightBearing, 0.0f));
                result.Y += static_cast<float>(lineSpacing_);
                curLineWidth = 0.0f;
                pendingRightBearing = 0.0f;
                finalLineHeight = static_cast<float>(lineSpacing_);
                firstInLine = true;
                continue;
            }

            const int index = getIndexForCharacter(c);

            const Vector3& cKern = kerning_[index];
            if (firstInLine)
            {
                // The first glyph of a line takes only a POSITIVE left side bearing, and
                // no Spacing. Measured against a live XNA 4.0 build (SAMPLE-031): with the
                // sample's 'A' and 'X', whose kerning.X is -1, XNA advances 0 and places the
                // glyph flush at the draw position; with 'B', whose kerning.X is +1, it
                // advances 1. MeasureString returns the same single-character width whether
                // that font's Spacing is 0, 3 or -2, so Spacing is not applied to a line's
                // first glyph either. FNA writes Math.Abs(cKern.X) here, which pushes a
                // negative bearing RIGHT by its magnitude instead of clamping it away.
                curLineWidth += std::max(cKern.X, 0.0f);
                firstInLine = false;
            }
            else
            {
                curLineWidth += spacing_ + cKern.X;
            }

            // The previous glyph's overhang, unclamped: between two glyphs it is real width.
            curLineWidth += pendingRightBearing;
            curLineWidth += cKern.Y;
            pendingRightBearing = cKern.Z;

            const int cCropHeight = croppingData_[index].Height;
            if (static_cast<float>(cCropHeight) > finalLineHeight)
            {
                finalLineHeight = static_cast<float>(cCropHeight);
            }
        }

        result.X = std::max(result.X, curLineWidth + std::max(pendingRightBearing, 0.0f));
        result.Y += finalLineHeight;

        return result;
    }

    Vector2 SpriteFont::MeasureString(const System::Text::StringBuilder& text) const
    {
        return MeasureString(text.ToString());
    }
}
