// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Style of a font described by FontDescription: regular, bold, italic, or a
     *        combination.
     */
    enum class FontDescriptionStyle : SharpRuntime::intcs
    {
        /** @brief The font is not styled. */
        Regular = 0,
        /** @brief The font is bold. */
        Bold = 1,
        /** @brief The font is italic. */
        Italic = 2
    };

    /**
     * @brief Combines two font styles, as the C# `[Flags]` enum's `|` does.
     *
     * @param left First style.
     * @param right Second style.
     * @return The combined style.
     */
    [[nodiscard]] constexpr FontDescriptionStyle operator|(FontDescriptionStyle left, FontDescriptionStyle right) noexcept
    {
        return static_cast<FontDescriptionStyle>(static_cast<SharpRuntime::intcs>(left) |
                                                 static_cast<SharpRuntime::intcs>(right));
    }

    /**
     * @brief Intersects two font styles, as the C# `[Flags]` enum's `&` does.
     *
     * @param left First style.
     * @param right Second style.
     * @return The common style bits.
     */
    [[nodiscard]] constexpr FontDescriptionStyle operator&(FontDescriptionStyle left, FontDescriptionStyle right) noexcept
    {
        return static_cast<FontDescriptionStyle>(static_cast<SharpRuntime::intcs>(left) &
                                                 static_cast<SharpRuntime::intcs>(right));
    }

    /**
     * @brief One inclusive range of characters a font is built for, as the `<CharacterRegion>`
     *        elements of a `.spritefont` document spell it.
     *
     * XNA keeps this type private to `FontDescription` and reaches it only through the
     * `CharacterRegions` element; it is named here because C++ has no private serializable
     * member. Nothing outside the font description uses it.
     */
    struct CNAEXT CharacterRegion
    {
        /** @brief .NET-style name, so the intermediate serializer can identify the type. */
        static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.CharacterRegion";

        /** @brief First character of the range. */
        SharpRuntime::charcs Start = u'\0';

        /** @brief Last character of the range, included in it. */
        SharpRuntime::charcs End = u'\0';

        /**
         * @brief Describes the region for the intermediate serializer.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(
            Serialization::Intermediate::ContentTypeDescriptor<CharacterRegion>& d);
    };

    /**
     * @brief Provides properties that define the description of a font, which the font processor
     *        turns into a SpriteFont: the `.spritefont` document is this type in intermediate XML.
     */
    class FontDescription : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescription";

        /**
         * @brief Initializes an empty font description, for the intermediate serializer to fill.
         *
         * XNA has this constructor too and keeps it private, because its serializer reaches a
         * private constructor by reflection. C++ has no reflection, so the serializer needs a
         * constructor it can call: this one. Prefer a constructor that names the font -- an
         * instance made here has no font name, and the deserializer supplies one before the
         * description is used.
         */
        CNAEXT FontDescription() = default;

        /**
         * @brief Initializes a new instance of FontDescription with the specified name, size and
         *        spacing, a regular style and no kerning.
         *
         * @param fontName The name of the font, such as "Times New Roman" or "Segoe UI".
         * @param size The size, in points, of the font.
         * @param spacing The amount of space, in pixels, to insert between letters.
         * @throws System::ArgumentNullException when the name is null or empty.
         * @throws System::ArgumentOutOfRangeException when the size is not greater than zero.
         */
        FontDescription(std::string fontName, SharpRuntime::Single size, SharpRuntime::Single spacing);

        /**
         * @brief Initializes a new instance of FontDescription with the specified name, size,
         *        spacing and style, and no kerning.
         *
         * @param fontName The name of the font.
         * @param size The size, in points, of the font.
         * @param spacing The amount of space, in pixels, to insert between letters.
         * @param fontStyle The style of the font.
         * @throws System::ArgumentNullException when the name is null or empty.
         * @throws System::ArgumentOutOfRangeException when the size is not greater than zero.
         */
        FontDescription(std::string fontName, SharpRuntime::Single size, SharpRuntime::Single spacing,
                        FontDescriptionStyle fontStyle);

        /**
         * @brief Initializes a new instance of FontDescription with the specified name, size,
         *        spacing, style and kerning.
         *
         * @param fontName The name of the font.
         * @param size The size, in points, of the font.
         * @param spacing The amount of space, in pixels, to insert between letters.
         * @param fontStyle The style of the font.
         * @param useKerning true to use kerning information when the font is built.
         * @throws System::ArgumentNullException when the name is null or empty.
         * @throws System::ArgumentOutOfRangeException when the size is not greater than zero.
         */
        FontDescription(std::string fontName, SharpRuntime::Single size, SharpRuntime::Single spacing,
                        FontDescriptionStyle fontStyle, bool useKerning);

        /**
         * @brief Gets the collection of characters provided by this font.
         *
         * The collection is the font description's own: adding to it adds the character to the
         * font, and a character added twice appears once.
         *
         * @return The characters, in ascending order.
         */
        [[nodiscard]] std::set<SharpRuntime::charcs>& getCharactersProperty() noexcept;

        /**
         * @brief Gets the collection of characters provided by this font.
         *
         * @return The characters, in ascending order.
         */
        [[nodiscard]] const std::set<SharpRuntime::charcs>& getCharactersProperty() const noexcept;

        /**
         * @brief Gets the default character for the font, used for every character the font does
         *        not provide.
         *
         * @return The default character, or an empty optional when the font has none.
         */
        [[nodiscard]] std::optional<SharpRuntime::charcs> getDefaultCharacterProperty() const noexcept;

        /**
         * @brief Sets the default character for the font.
         *
         * @param value The default character, or an empty optional for none.
         */
        void setDefaultCharacterProperty(std::optional<SharpRuntime::charcs> value) noexcept;

        /**
         * @brief Gets the name of the font, such as "Times New Roman" or "Segoe UI".
         *
         * @return The font name.
         */
        [[nodiscard]] const std::string& getFontNameProperty() const noexcept;

        /**
         * @brief Sets the name of the font.
         *
         * @param value The font name.
         * @throws System::ArgumentNullException when the name is null or empty.
         */
        void setFontNameProperty(std::string value);

        /**
         * @brief Gets the size, in points, of the font.
         *
         * @return The font size.
         */
        [[nodiscard]] SharpRuntime::Single getSizeProperty() const noexcept;

        /**
         * @brief Sets the size, in points, of the font.
         *
         * @param value The font size.
         * @throws System::ArgumentOutOfRangeException when the size is not greater than zero.
         */
        void setSizeProperty(SharpRuntime::Single value);

        /**
         * @brief Gets the amount of space, in pixels, to insert between letters.
         *
         * @return The letter spacing.
         */
        [[nodiscard]] SharpRuntime::Single getSpacingProperty() const noexcept;

        /**
         * @brief Sets the amount of space, in pixels, to insert between letters.
         *
         * @param value The letter spacing.
         */
        void setSpacingProperty(SharpRuntime::Single value) noexcept;

        /**
         * @brief Gets the style of the font.
         *
         * @return The font style.
         */
        [[nodiscard]] FontDescriptionStyle getStyleProperty() const noexcept;

        /**
         * @brief Sets the style of the font.
         *
         * @param value The font style.
         */
        void setStyleProperty(FontDescriptionStyle value) noexcept;

        /**
         * @brief Gets whether to use kerning information when the font is built.
         *
         * @return true when kerning information is used.
         */
        [[nodiscard]] bool getUseKerningProperty() const noexcept;

        /**
         * @brief Sets whether to use kerning information when the font is built.
         *
         * @param value true to use kerning information.
         */
        void setUseKerningProperty(bool value) noexcept;

        /**
         * @brief Gets the characters of this font as the inclusive ranges the intermediate format
         *        writes, each run of consecutive characters merged into one region.
         *
         * @return The regions, in ascending order.
         */
        CNAEXT [[nodiscard]] std::vector<CharacterRegion> getCharacterRegionsProperty() const;

        /**
         * @brief Replaces the characters of this font with the ones the given regions cover.
         *
         * @param value The regions, each inclusive; overlapping regions contribute one character
         *        each, as a set.
         * @throws System::Exception when a region starts after it ends.
         */
        CNAEXT void setCharacterRegionsProperty(const std::vector<CharacterRegion>& value);

        /**
         * @brief Describes the font for the intermediate serializer, in the order the measured
         *        `.spritefont` format writes: FontName, Size, Spacing, UseKerning, Style,
         *        DefaultCharacter, CharacterRegions.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<FontDescription>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string fontName_;
        SharpRuntime::Single size_ = 0.0f;
        SharpRuntime::Single spacing_ = 0.0f;
        FontDescriptionStyle style_ = FontDescriptionStyle::Regular;
        bool useKerning_ = false;
        std::optional<SharpRuntime::charcs> defaultCharacter_;
        std::set<SharpRuntime::charcs> characters_;
    };
}

CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescriptionStyle,
                     "Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescriptionStyle", true,
                     {Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescriptionStyle::Regular, "Regular"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescriptionStyle::Bold, "Bold"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescriptionStyle::Italic, "Italic"});
