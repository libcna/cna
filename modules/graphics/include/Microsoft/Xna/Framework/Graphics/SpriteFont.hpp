// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Text/StringBuilder.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using SharpRuntime::charcs;
    using SharpRuntime::String;

    /** @brief Represents a font texture used to draw text with SpriteBatch. */
    class SpriteFont
    {
    public:
        /**
         * @brief Constructs a SpriteFont from its glyph atlas and layout tables.
         *
         * In XNA this constructor is internal and only invoked by the content
         * pipeline's SpriteFontReader. CNA has no XNB pipeline, so it is exposed
         * for content readers (or applications) that build the atlas themselves.
         * The four list parameters must all have the same length, one entry per
         * glyph in @p characters.
         *
         * @param texture     Glyph atlas texture (owned elsewhere; not copied).
         * @param glyphBounds Source rectangle of each glyph within the atlas.
         * @param cropping    Per-glyph offset/cropping rectangle.
         * @param characters  Sorted list of characters this font can render.
         * @param lineSpacing Vertical distance between text lines, in pixels.
         * @param spacing     Extra horizontal spacing applied between characters.
         * @param kerningData Per-glyph (left bearing, width, right bearing).
         * @param defaultCharacter Fallback glyph, or std::nullopt to throw on misses.
         * @throws System::ArgumentException if @p defaultCharacter has a value not present in
         *         @p characters.
         */
        CNAEXT SpriteFont(Texture2D texture,
                         std::vector<Rectangle> glyphBounds,
                         std::vector<Rectangle> cropping,
                         std::vector<charcs> characters,
                         int lineSpacing,
                         float spacing,
                         std::vector<Vector3> kerningData,
                         std::optional<charcs> defaultCharacter);

        /**
         * @brief Gets the collection of characters this font can render.
         * @return Read-only reference to the character list.
         */
        [[nodiscard]] const std::vector<charcs>& getCharactersProperty() const;

        /**
         * @brief Gets the glyph atlas texture this font draws from.
         *
         * XNA keeps the atlas internal because `SpriteBatch` is the only thing that needs it. A
         * consumer that has to re-implement text layout above this class -- a language binding
         * whose own `SpriteBatch.DrawString` places each glyph itself -- needs the same four
         * tables `SpriteBatch` reads, and this is the first of them. Together with
         * getGlyphBoundsEXT(), getCroppingEXT(), getKerningEXT() and getCharactersProperty() they
         * round-trip exactly what the constructor accepts.
         *
         * @return Read-only reference to the atlas texture, valid while this font is.
         */
        CNAEXT [[nodiscard]] const Texture2D& getTextureEXT() const;

        /**
         * @brief Gets each glyph's source rectangle within the atlas texture.
         * @return Read-only reference, one entry per character in getCharactersProperty(), in the
         *         same order.
         */
        CNAEXT [[nodiscard]] const std::vector<Rectangle>& getGlyphBoundsEXT() const;

        /**
         * @brief Gets each glyph's cropping/offset rectangle.
         * @return Read-only reference, one entry per character in getCharactersProperty(), in the
         *         same order.
         */
        CNAEXT [[nodiscard]] const std::vector<Rectangle>& getCroppingEXT() const;

        /**
         * @brief Gets each glyph's left bearing, width and right bearing.
         * @return Read-only reference, one entry per character in getCharactersProperty(), in the
         *         same order.
         */
        CNAEXT [[nodiscard]] const std::vector<Vector3>& getKerningEXT() const;

        /**
         * @brief Gets the fallback character used when a requested character is not in the font.
         * @return Optional fallback character; std::nullopt if unset (throws on miss).
         */
        [[nodiscard]] std::optional<charcs> getDefaultCharacterProperty() const;
        /**
         * @brief Sets the fallback character used when a requested character is not in the font.
         * @param value Optional fallback character; pass std::nullopt to throw on misses.
         * @throws System::ArgumentException if @p value is set but not present in this font's
         *         character list.
         */
        void setDefaultCharacterProperty(std::optional<charcs> value);

        /**
         * @brief Gets the vertical distance in pixels between the base lines of two consecutive lines of text.
         * @return Current line spacing in pixels.
         */
        [[nodiscard]] int getLineSpacingProperty() const;
        /**
         * @brief Sets the vertical distance in pixels between the base lines of two consecutive lines of text.
         * @param value New line spacing in pixels.
         */
        void setLineSpacingProperty(int value);

        /**
         * @brief Gets the extra horizontal spacing in pixels applied between characters.
         * @return Current character spacing offset.
         */
        [[nodiscard]] float getSpacingProperty() const;
        /**
         * @brief Sets the extra horizontal spacing in pixels applied between characters.
         * @param value New character spacing offset.
         */
        void setSpacingProperty(float value);

        /**
         * @brief Measures the size of a string when drawn with this font.
         *
         * @param text The text to measure.
         * @return The width and height of the rendered text, in pixels.
         * @throws std::invalid_argument if @p text contains a character this font cannot render
         *         and no defaultCharacter is set.
         */
        [[nodiscard]] Vector2 MeasureString(const String& text) const;

        /**
         * @brief Measures the size of a StringBuilder's text when drawn with this font.
         *
         * @param text The text to measure.
         * @return The width and height of the rendered text, in pixels.
         * @throws std::invalid_argument if @p text contains a character this font cannot render
         *         and no defaultCharacter is set.
         */
        [[nodiscard]] Vector2 MeasureString(const System::Text::StringBuilder& text) const;

    private:
        Texture2D textureValue_;
        std::vector<Rectangle> glyphData_;
        std::vector<Rectangle> croppingData_;
        std::vector<Vector3> kerning_;
        std::vector<charcs> characterMap_;
        std::unordered_map<charcs, int> characterIndexMap_;
        std::optional<charcs> defaultCharacter_;
        int lineSpacing_ = 0;
        float spacing_   = 0.0f;

        friend class SpriteBatch;
    };
}
