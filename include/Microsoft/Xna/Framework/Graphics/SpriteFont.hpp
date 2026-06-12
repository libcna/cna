// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using SharpRuntime::charcs;
    using SharpRuntime::String;

    /// Represents a font texture used to draw text with SpriteBatch.
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
         */
        NOXNA SpriteFont(Texture2D texture,
                         std::vector<Rectangle> glyphBounds,
                         std::vector<Rectangle> cropping,
                         std::vector<charcs> characters,
                         int lineSpacing,
                         float spacing,
                         std::vector<Vector3> kerningData,
                         std::optional<charcs> defaultCharacter);

        /// XNA 4.0: SpriteFont.Characters — the characters this font can render.
        [[nodiscard]] const std::vector<charcs>& getCharactersProperty() const;

        /// XNA 4.0: SpriteFont.DefaultCharacter — fallback glyph for missing chars.
        [[nodiscard]] std::optional<charcs> getDefaultCharacterProperty() const;
        void setDefaultCharacterProperty(std::optional<charcs> value);

        /// XNA 4.0: SpriteFont.LineSpacing
        [[nodiscard]] int getLineSpacingProperty() const;
        void setLineSpacingProperty(int value);

        /// XNA 4.0: SpriteFont.Spacing
        [[nodiscard]] float getSpacingProperty() const;
        void setSpacingProperty(float value);

        /// Measures the size of a string when drawn with this font.
        [[nodiscard]] Vector2 MeasureString(const String& text) const;

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
