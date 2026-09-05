// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/FontDescription.hpp"

#include <sstream>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Exception.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        /** @brief The .NET Framework's `float.ToString()`, as the size refusal quotes the value. */
        std::string FormatSize(SharpRuntime::Single value)
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text << value;
            return text.str();
        }

        /** @brief `'c' (0x0063)`, the way the region refusal names a character. */
        std::string FormatCharacter(SharpRuntime::charcs value)
        {
            static const char* digits = "0123456789abcdef";
            std::string hex(4, '0');
            auto code = static_cast<std::uint16_t>(value);
            for (int i = 3; i >= 0; --i)
            {
                hex[static_cast<std::size_t>(i)] = digits[code & 0xFU];
                code = static_cast<std::uint16_t>(code >> 4);
            }
            std::string text = "'";
            if (value < 0x80)
            {
                text += static_cast<char>(value);
            }
            else
            {
                text += '?';
            }
            text += "' (0x" + hex + ")";
            return text;
        }
    }

    void CharacterRegion::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<CharacterRegion>& d)
    {
        d.Field("Start", &CharacterRegion::Start);
        d.Field("End", &CharacterRegion::End);
    }

    FontDescription::FontDescription(std::string fontName, SharpRuntime::Single size, SharpRuntime::Single spacing)
        : FontDescription(std::move(fontName), size, spacing, FontDescriptionStyle::Regular, false)
    {
    }

    FontDescription::FontDescription(std::string fontName, SharpRuntime::Single size, SharpRuntime::Single spacing,
                                     FontDescriptionStyle fontStyle)
        : FontDescription(std::move(fontName), size, spacing, fontStyle, false)
    {
    }

    FontDescription::FontDescription(std::string fontName, SharpRuntime::Single size, SharpRuntime::Single spacing,
                                     FontDescriptionStyle fontStyle, bool useKerning)
    {
        // The constructors assign through the property setters, which is why their refusals name
        // the parameter `value` rather than `fontName` or `size`.
        setFontNameProperty(std::move(fontName));
        setSizeProperty(size);
        setSpacingProperty(spacing);
        setStyleProperty(fontStyle);
        setUseKerningProperty(useKerning);
    }

    std::set<SharpRuntime::charcs>& FontDescription::getCharactersProperty() noexcept { return characters_; }

    const std::set<SharpRuntime::charcs>& FontDescription::getCharactersProperty() const noexcept
    {
        return characters_;
    }

    std::optional<SharpRuntime::charcs> FontDescription::getDefaultCharacterProperty() const noexcept
    {
        return defaultCharacter_;
    }

    void FontDescription::setDefaultCharacterProperty(std::optional<SharpRuntime::charcs> value) noexcept
    {
        defaultCharacter_ = value;
    }

    const std::string& FontDescription::getFontNameProperty() const noexcept { return fontName_; }

    void FontDescription::setFontNameProperty(std::string value)
    {
        if (value.empty())
        {
            throw System::ArgumentNullException("value", "FontName is required, and cannot be null or empty.");
        }
        fontName_ = std::move(value);
    }

    SharpRuntime::Single FontDescription::getSizeProperty() const noexcept { return size_; }

    void FontDescription::setSizeProperty(SharpRuntime::Single value)
    {
        // A NaN size is accepted, because the runtime's guard is `value <= 0` and no comparison
        // with NaN is true (measured: font/ctor_nan_size).
        if (value <= 0.0f)
        {
            throw System::ArgumentOutOfRangeException(
                "value", "The value " + FormatSize(value) + " is invalid. Font size must be greater than zero.");
        }
        size_ = value;
    }

    SharpRuntime::Single FontDescription::getSpacingProperty() const noexcept { return spacing_; }

    void FontDescription::setSpacingProperty(SharpRuntime::Single value) noexcept { spacing_ = value; }

    FontDescriptionStyle FontDescription::getStyleProperty() const noexcept { return style_; }

    void FontDescription::setStyleProperty(FontDescriptionStyle value) noexcept { style_ = value; }

    bool FontDescription::getUseKerningProperty() const noexcept { return useKerning_; }

    void FontDescription::setUseKerningProperty(bool value) noexcept { useKerning_ = value; }

    std::vector<CharacterRegion> FontDescription::getCharacterRegionsProperty() const
    {
        std::vector<CharacterRegion> regions;
        for (const SharpRuntime::charcs character : characters_)
        {
            if (!regions.empty() && static_cast<std::uint16_t>(regions.back().End) + 1 ==
                                        static_cast<std::uint16_t>(character))
            {
                regions.back().End = character;
                continue;
            }
            regions.push_back(CharacterRegion{character, character});
        }
        return regions;
    }

    void FontDescription::setCharacterRegionsProperty(const std::vector<CharacterRegion>& value)
    {
        std::set<SharpRuntime::charcs> characters;
        for (const CharacterRegion& region : value)
        {
            if (region.Start > region.End)
            {
                throw System::Exception("The value is invalid. Start, " + FormatCharacter(region.Start) +
                                        ", must be less than or equal to End, " + FormatCharacter(region.End) + ".");
            }
            for (auto code = static_cast<std::uint32_t>(region.Start); code <= static_cast<std::uint32_t>(region.End);
                 ++code)
            {
                characters.insert(static_cast<SharpRuntime::charcs>(code));
            }
        }
        characters_ = std::move(characters);
    }

    void FontDescription::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<FontDescription>& d)
    {
        // The order, and which elements a document may leave out, are measured
        // (tests/reference/xna40/graphics, cases font/serialize* and font/deserialize_spritefont*).
        // Characters carries [ContentSerializerIgnore] in XNA and is not described at all here.
        d.Property("FontName", &FontDescription::getFontNameProperty, &FontDescription::setFontNameProperty)
            .AllowNull(false);
        d.Property("Size", &FontDescription::getSizeProperty, &FontDescription::setSizeProperty);
        d.Property("Spacing", &FontDescription::getSpacingProperty, &FontDescription::setSpacingProperty).Optional();
        d.Property("UseKerning", &FontDescription::getUseKerningProperty, &FontDescription::setUseKerningProperty)
            .Optional();
        d.Property("Style", &FontDescription::getStyleProperty, &FontDescription::setStyleProperty);
        d.Property("DefaultCharacter", &FontDescription::getDefaultCharacterProperty,
                   &FontDescription::setDefaultCharacterProperty)
            .Optional();
        d.Property("CharacterRegions", &FontDescription::getCharacterRegionsProperty,
                   &FontDescription::setCharacterRegionsProperty)
            .CollectionItemName("CharacterRegion");
    }

    const std::string& FontDescription::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
