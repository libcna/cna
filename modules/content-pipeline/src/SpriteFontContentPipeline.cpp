// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>

#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/TextureCompressionPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Xml.hpp"

#if defined(CNA_HAVE_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#endif

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kImporterName = "CNA.FontDescriptionImporter";
        constexpr const char* kProcessorName = "CNA.FontDescriptionProcessor";

        /** @brief Largest atlas side this pipeline will produce, matching XNA's Reach limit. */
        constexpr std::uint32_t kMaximumAtlasSide = 2048u;

        /** @brief Largest number of glyphs one font may carry. */
        constexpr std::size_t kMaximumGlyphCount = 8192u;

        [[noreturn]] void Fail(const std::string& origin, const std::string& reason)
        {
            throw std::runtime_error(origin + ": " + reason);
        }

        /**
         * @brief Decodes the single character an element's text denotes.
         *
         * `.spritefont` files write a character either literally or as a numeric reference; the
         * XML reader has already resolved the reference, so this only has to reject text that is
         * not exactly one code unit.
         *
         * @param text Already-entity-resolved element text.
         * @param origin Path used in diagnostics.
         * @param what Element name used in diagnostics.
         * @return The character.
         */
        [[nodiscard]] SharpRuntime::charcs ParseCharacter(
            const std::string& text, const std::string& origin, const char* what)
        {
            if (text.empty()) { Fail(origin, std::string(what) + " is empty"); }
            const auto lead = static_cast<unsigned char>(text[0]);
            std::uint32_t code = 0u;
            std::size_t length = 1u;
            if (lead < 0x80u) { code = lead; }
            else if (lead >= 0xC2u && lead <= 0xDFu) { code = lead & 0x1Fu; length = 2u; }
            else if (lead >= 0xE0u && lead <= 0xEFu) { code = lead & 0x0Fu; length = 3u; }
            else { Fail(origin, std::string(what) + " is not valid UTF-8"); }
            if (text.size() != length)
            {
                Fail(origin, std::string(what) + " must be exactly one character, but is '" +
                                 text + "'");
            }
            for (std::size_t index = 1u; index < length; ++index)
            {
                const auto continuation = static_cast<unsigned char>(text[index]);
                if ((continuation & 0xC0u) != 0x80u)
                {
                    Fail(origin, std::string(what) + " is not valid UTF-8");
                }
                code = (code << 6) | (continuation & 0x3Fu);
            }
            if (code > 0xFFFFu)
            {
                Fail(origin, std::string(what) +
                                 " is outside the Basic Multilingual Plane; a SpriteFont stores "
                                 "UTF-16 code units and cannot hold a surrogate pair");
            }
            return static_cast<SharpRuntime::charcs>(code);
        }

        [[nodiscard]] float ParseFloat(const std::string& text, const std::string& origin,
                                       const char* what)
        {
            try
            {
                std::size_t consumed = 0u;
                const float value = std::stof(text, &consumed);
                if (consumed != text.size() || !std::isfinite(value))
                {
                    Fail(origin, std::string(what) + " is not a finite number: '" + text + "'");
                }
                return value;
            }
            catch (const std::runtime_error&)
            {
                Fail(origin, std::string(what) + " is not a number: '" + text + "'");
            }
        }

        [[nodiscard]] bool ParseBoolean(const std::string& text, const std::string& origin,
                                        const char* what)
        {
            if (text == "true") { return true; }
            if (text == "false") { return false; }
            Fail(origin, std::string(what) + " must be 'true' or 'false', not '" + text + "'");
        }
    }

    FontDescription ParseFontDescription(const std::string& xml, const std::string& origin)
    {
        const CNA::Internal::XmlElement root = CNA::Internal::ParseXml(xml, origin);
        if (root.name != "XnaContent")
        {
            Fail(origin, "the root element is '" + root.name + "', not 'XnaContent'");
        }
        const CNA::Internal::XmlElement* asset = root.Find("Asset");
        if (asset == nullptr) { Fail(origin, "there is no <Asset> element"); }

        const auto type = asset->attributes.find("Type");
        if (type == asset->attributes.end())
        {
            Fail(origin, "<Asset> has no Type attribute");
        }
        // XNA writes the namespace-prefixed "Graphics:FontDescription"; the prefix is bound in
        // the root element and carries no other information, so the local name is what matters.
        const std::size_t colon = type->second.rfind(':');
        const std::string localType =
            colon == std::string::npos ? type->second : type->second.substr(colon + 1u);
        if (localType != "FontDescription" && localType != "LocalizedFontDescription")
        {
            Fail(origin, "<Asset Type=\"" + type->second +
                             "\"> is not a font description; a .spritefont declares "
                             "Graphics:FontDescription");
        }

        // XNA's XML serializer writes each field exactly once. A duplicate is a hand-editing
        // mistake, and quietly honouring the first occurrence hides it -- the later one is
        // usually what the author meant, so neither choice is safe to make silently.
        const auto single = [&](const char* name) -> const CNA::Internal::XmlElement*
        {
            const std::vector<const CNA::Internal::XmlElement*> found = asset->FindAll(name);
            if (found.size() > 1u)
            {
                Fail(origin, std::string("<") + name + "> appears " +
                                 std::to_string(found.size()) +
                                 " times; a font description declares it at most once");
            }
            return found.empty() ? nullptr : found.front();
        };

        FontDescription description;
        if (const CNA::Internal::XmlElement* element = single("FontName"))
        {
            description.fontName = element->TrimmedText();
        }
        if (description.fontName.empty()) { Fail(origin, "<FontName> is missing or empty"); }

        if (const CNA::Internal::XmlElement* element = single("Size"))
        {
            description.size = ParseFloat(element->TrimmedText(), origin, "<Size>");
        }
        else
        {
            Fail(origin, "<Size> is missing");
        }
        if (!(description.size > 0.0f) || description.size > 1024.0f)
        {
            Fail(origin, "<Size> must be greater than 0 and at most 1024");
        }

        if (const CNA::Internal::XmlElement* element = single("Spacing"))
        {
            description.spacing = ParseFloat(element->TrimmedText(), origin, "<Spacing>");
        }
        if (const CNA::Internal::XmlElement* element = single("UseKerning"))
        {
            description.useKerning =
                ParseBoolean(element->TrimmedText(), origin, "<UseKerning>");
        }
        if (const CNA::Internal::XmlElement* element = single("Style"))
        {
            std::string style = element->TrimmedText();
            style.erase(std::remove_if(style.begin(), style.end(),
                                       [](const char character)
                                       { return character == ' ' || character == '\t'; }),
                        style.end());
            std::transform(style.begin(), style.end(), style.begin(),
                           [](const unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            if (style.empty() || style == "regular")
            {
                description.style = FontDescriptionStyle::Regular;
            }
            else if (style == "bold") { description.style = FontDescriptionStyle::Bold; }
            else if (style == "italic") { description.style = FontDescriptionStyle::Italic; }
            else if (style == "bold,italic" || style == "italic,bold")
            {
                description.style = FontDescriptionStyle::BoldItalic;
            }
            else
            {
                Fail(origin, "<Style> must be Regular, Bold, Italic or \"Bold, Italic\", not '" +
                                 element->TrimmedText() + "'");
            }
        }
        if (const CNA::Internal::XmlElement* element = single("DefaultCharacter"))
        {
            description.defaultCharacter =
                ParseCharacter(element->text, origin, "<DefaultCharacter>");
        }

        const CNA::Internal::XmlElement* regions = single("CharacterRegions");
        if (regions == nullptr)
        {
            Fail(origin, "<CharacterRegions> is missing; a SpriteFont with no characters cannot "
                         "draw anything");
        }
        for (const CNA::Internal::XmlElement* region : regions->FindAll("CharacterRegion"))
        {
            const CNA::Internal::XmlElement* start = region->Find("Start");
            const CNA::Internal::XmlElement* end = region->Find("End");
            if (start == nullptr || end == nullptr)
            {
                Fail(origin, "a <CharacterRegion> needs both <Start> and <End>");
            }
            FontCharacterRegion parsed;
            parsed.start = ParseCharacter(start->text, origin, "<Start>");
            parsed.end = ParseCharacter(end->text, origin, "<End>");
            if (parsed.end < parsed.start)
            {
                Fail(origin, "a <CharacterRegion> ends before it starts");
            }
            description.characterRegions.push_back(parsed);
        }
        if (description.characterRegions.empty())
        {
            Fail(origin, "<CharacterRegions> contains no <CharacterRegion>");
        }
        return description;
    }

    std::vector<SharpRuntime::charcs> ExpandCharacterRegions(const FontDescription& description)
    {
        std::set<SharpRuntime::charcs> characters;
        for (const FontCharacterRegion& region : description.characterRegions)
        {
            for (std::uint32_t code = region.start; code <= region.end; ++code)
            {
                characters.insert(static_cast<SharpRuntime::charcs>(code));
                if (characters.size() > kMaximumGlyphCount)
                {
                    throw std::runtime_error(
                        "a SpriteFont may carry at most " + std::to_string(kMaximumGlyphCount) +
                        " characters; the described regions ask for more");
                }
            }
        }
        // A <DefaultCharacter> outside every region joins them rather than failing the build. The
        // old refusal reasoned that such a font could never draw its own fallback, which was true
        // only because CNA declined to put it there; XNA adds it, and the character list it writes
        // is sorted, so the fallback lands wherever its code point belongs. Measured on
        // font_regions.spritefont, whose regions cover 42 characters and whose font carries 43
        // (plans/plan_xnapipeline_parity.md XNAPP-182).
        if (description.defaultCharacter.has_value())
        {
            characters.insert(*description.defaultCharacter);
            if (characters.size() > kMaximumGlyphCount)
            {
                throw std::runtime_error(
                    "a SpriteFont may carry at most " + std::to_string(kMaximumGlyphCount) +
                    " characters; the described regions and the default character ask for more");
            }
        }
        return {characters.begin(), characters.end()};
    }
}

namespace CNA::Content::Pipeline
{
    bool IsFontRasterizationAvailable() noexcept
    {
#if defined(CNA_HAVE_FREETYPE)
        return true;
#else
        return false;
#endif
    }

#if defined(CNA_HAVE_FREETYPE)
    namespace
    {
        /** @brief RAII owner for the FreeType library handle. */
        class FreeTypeLibrary
        {
        public:
            FreeTypeLibrary()
            {
                if (FT_Init_FreeType(&library_) != 0)
                {
                    throw std::runtime_error("FreeType could not be initialized");
                }
            }

            ~FreeTypeLibrary() { FT_Done_FreeType(library_); }

            FreeTypeLibrary(const FreeTypeLibrary&) = delete;
            FreeTypeLibrary& operator=(const FreeTypeLibrary&) = delete;

            [[nodiscard]] FT_Library Handle() const noexcept { return library_; }

        private:
            FT_Library library_ = nullptr;
        };

        /** @brief RAII owner for one opened face. */
        class FreeTypeFace
        {
        public:
            FreeTypeFace(const FT_Library library, const std::filesystem::path& path)
            {
                const std::string native = CNA::Internal::ContentPathToUtf8(path);
                if (FT_New_Face(library, native.c_str(), 0, &face_) != 0)
                {
                    throw std::runtime_error("'" + native +
                                             "' could not be opened as a font file");
                }
            }

            ~FreeTypeFace() { FT_Done_Face(face_); }

            FreeTypeFace(const FreeTypeFace&) = delete;
            FreeTypeFace& operator=(const FreeTypeFace&) = delete;

            [[nodiscard]] FT_Face Handle() const noexcept { return face_; }

        private:
            FT_Face face_ = nullptr;
        };

        /** @brief One rasterized glyph, before packing. */
        struct RasterGlyph
        {
            SharpRuntime::charcs character = u'\0';
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            std::vector<std::uint8_t> coverage;
            int leftBearing = 0;
            int topBearing = 0;
            int advance = 0;
            bool blank = false;
        };

        /** @brief Returns the smallest power of two that is at least @p value. */
        [[nodiscard]] std::uint32_t RoundUpToPowerOfTwo(std::uint32_t value) noexcept
        {
            std::uint32_t result = 1u;
            while (result < value && result < kMaximumAtlasSide) { result <<= 1; }
            return result;
        }

        /** @brief Where one glyph landed in the atlas. */
        struct PackedGlyph
        {
            std::uint32_t x = 0u;
            std::uint32_t y = 0u;
        };

        /**
         * @brief Packs glyphs into a square atlas with a deterministic shelf algorithm.
         *
         * Glyphs are placed in character order, not sorted by size: a build must produce the same
         * atlas every time, and character order is the one ordering the caller can also predict.
         *
         * @param glyphs Rasterized glyphs, in character order.
         * @param side Atlas side length to try.
         * @param placements Receives one placement per glyph when the pack succeeds.
         * @param usedHeight Receives the number of rows actually occupied.
         * @return True when every glyph fits.
         */
        [[nodiscard]] bool PackGlyphs(const std::vector<RasterGlyph>& glyphs,
                                       const std::uint32_t width, const std::uint32_t maximumHeight,
                                       std::vector<PackedGlyph>& placements,
                                       std::uint32_t& usedHeight)
        {
            constexpr std::uint32_t kPadding = 1u;
            placements.assign(glyphs.size(), PackedGlyph{});
            // Tallest first. A shelf is as tall as its tallest member, so packing in character
            // order lets one accented capital raise the shelf every other glyph on it sits in;
            // ordering by height keeps each shelf close to the height of what is on it. The
            // glyphs themselves are not reordered -- only the order they are placed in -- so a
            // glyph's rectangle still belongs to its own character.
            std::vector<std::size_t> order(glyphs.size());
            for (std::size_t index = 0u; index < order.size(); ++index) { order[index] = index; }
            std::stable_sort(order.begin(), order.end(),
                             [&glyphs](const std::size_t left, const std::size_t right)
                             { return glyphs[left].height > glyphs[right].height; });

            std::uint32_t penX = kPadding;
            std::uint32_t penY = kPadding;
            std::uint32_t shelfHeight = 0u;
            for (const std::size_t index : order)
            {
                const RasterGlyph& glyph = glyphs[index];
                if (glyph.width + kPadding > width || glyph.height + kPadding > maximumHeight)
                {
                    return false;
                }
                if (penX + glyph.width + kPadding > width)
                {
                    penX = kPadding;
                    penY += shelfHeight + kPadding;
                    shelfHeight = 0u;
                }
                if (penY + glyph.height + kPadding > maximumHeight) { return false; }
                placements[index] = {penX, penY};
                penX += glyph.width + kPadding;
                shelfHeight = std::max(shelfHeight, glyph.height);
            }
            usedHeight = penY + shelfHeight + kPadding;
            return true;
        }
    }

    Cnb::CnbSpriteFontData RasterizeFontDescription(const FontDescription& description,
                                                    std::vector<std::string>& warnings,
                                                    const ContentStrictness strictness)
    {
        const std::vector<SharpRuntime::charcs> characters =
            ExpandCharacterRegions(description);
        const std::string origin =
            CNA::Internal::ContentPathToUtf8(description.resolvedFontFile);

        FreeTypeLibrary library;
        FreeTypeFace face(library.Handle(), description.resolvedFontFile);

        if (FT_Set_Char_Size(face.Handle(), 0,
                             static_cast<FT_F26Dot6>(std::lround(description.size * 64.0)),
                             96u, 96u) != 0)
        {
            throw std::runtime_error("'" + origin + "' cannot be rasterized at size " +
                                     std::to_string(description.size));
        }

        const bool wantsBold = description.style == FontDescriptionStyle::Bold ||
                               description.style == FontDescriptionStyle::BoldItalic;
        const bool wantsItalic = description.style == FontDescriptionStyle::Italic ||
                                 description.style == FontDescriptionStyle::BoldItalic;
        const bool faceIsBold = (face.Handle()->style_flags & FT_STYLE_FLAG_BOLD) != 0;
        const bool faceIsItalic = (face.Handle()->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
        const bool synthesizeBold = wantsBold && !faceIsBold;
        const bool synthesizeItalic = wantsItalic && !faceIsItalic;
        if (synthesizeBold || synthesizeItalic)
        {
            warnings.push_back(
                "'" + origin + "' is not a " +
                std::string(synthesizeBold && synthesizeItalic ? "bold italic"
                                                               : (synthesizeBold ? "bold"
                                                                                 : "italic")) +
                " face, so that emphasis is synthesized from the regular outlines. XNA would "
                "have selected the real styled face from the installed font family; point "
                "<FontName> at the styled font file to get it.");
        }

        // A 12-degree shear, the conventional synthetic-oblique angle, expressed in FreeType's
        // 16.16 fixed point.
        FT_Matrix oblique{0x10000, static_cast<FT_Fixed>(0.2126 * 0x10000), 0, 0x10000};

        std::vector<RasterGlyph> glyphs;
        glyphs.reserve(characters.size());
        for (const SharpRuntime::charcs character : characters)
        {
            FT_UInt glyphIndex =
                FT_Get_Char_Index(face.Handle(), static_cast<FT_ULong>(character));
            if (glyphIndex == 0u)
            {
                const std::string codepoint =
                    "U+" + [character]
                    {
                        static constexpr char kDigits[] = "0123456789ABCDEF";
                        std::string text(4u, '0');
                        for (std::size_t index = 0u; index < 4u; ++index)
                        {
                            text[index] =
                                kDigits[(static_cast<std::uint32_t>(character) >>
                                         ((3u - index) * 4u)) & 0xFu];
                        }
                        return text;
                    }();
                if (strictness != ContentStrictness::XnaCompatible)
                {
                    throw std::runtime_error(
                        "'" + origin + "' has no glyph for " + codepoint +
                        ". A SpriteFont must be able to draw every character its regions ask "
                        "for; narrow <CharacterRegions> or choose a font that covers them.");
                }
                // XNA builds this font. It draws the missing characters from the machine's own
                // default font and warns that redistributing the result may not be licensed
                // (measured: tests/reference/xna40/differential-errors/, font/missing_glyph).
                // Reaching for a second font would make the build depend on what is installed,
                // which is the one thing this pipeline will not do, so the character is drawn
                // with the font's own glyph 0 -- the box a font supplies for exactly this. The
                // font ends up covering the same characters XNA's does; what is inside the cell
                // differs, and the warning says so (plans/plan_xnapipeline_parity.md XNAPP-267).
                warnings.push_back(
                    "'" + origin + "' has no glyph for " + codepoint +
                    ", so it is drawn with the font's own .notdef glyph. XNA substitutes the "
                    "machine's default font here; CNA does not, because a content build must "
                    "produce the same bytes on every machine. Narrow <CharacterRegions> or "
                    "choose a font that covers them.");
                glyphIndex = 0u;
            }
            if (FT_Load_Glyph(face.Handle(), glyphIndex, FT_LOAD_DEFAULT) != 0)
            {
                throw std::runtime_error("'" + origin + "' failed to load a glyph outline");
            }
            if (synthesizeItalic && face.Handle()->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
            {
                FT_Outline_Transform(&face.Handle()->glyph->outline, &oblique);
            }
            if (synthesizeBold && face.Handle()->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
            {
                FT_Outline_Embolden(&face.Handle()->glyph->outline,
                                    face.Handle()->size->metrics.y_ppem * 64 / 24);
            }
            if (FT_Render_Glyph(face.Handle()->glyph, FT_RENDER_MODE_NORMAL) != 0)
            {
                throw std::runtime_error("'" + origin + "' failed to rasterize a glyph");
            }

            const FT_GlyphSlot slot = face.Handle()->glyph;
            RasterGlyph glyph;
            glyph.character = character;
            glyph.width = slot->bitmap.width;
            glyph.height = slot->bitmap.rows;
            glyph.leftBearing = slot->bitmap_left;
            glyph.topBearing = slot->bitmap_top;
            glyph.advance = static_cast<int>(slot->advance.x >> 6);
            if (glyph.width == 0u || glyph.height == 0u)
            {
                // A space has no ink. XNA's own fonts still reserve a texel for it so that every
                // character has a real source rectangle; the texel is fully transparent, so where
                // it lands cannot be seen.
                glyph.blank = true;
                glyph.width = 1u;
                glyph.height = 1u;
                glyph.leftBearing = 0;
                glyph.topBearing = 0;
                glyph.coverage.assign(1u, 0u);
            }
            else
            {
                glyph.coverage.resize(static_cast<std::size_t>(glyph.width) * glyph.height);
                for (std::uint32_t row = 0u; row < glyph.height; ++row)
                {
                    const unsigned char* source =
                        slot->bitmap.buffer + static_cast<std::ptrdiff_t>(row) * slot->bitmap.pitch;
                    std::memcpy(glyph.coverage.data() +
                                    static_cast<std::size_t>(row) * glyph.width,
                                source, glyph.width);
                }
            }
            glyphs.push_back(std::move(glyph));
        }

        // The sheet is as narrow as the glyphs allow and grows downwards, which is the shape XNA's
        // own atlases have: 128x64 at size 10, 128x128 at 14, 128x256 at 18, 256x256 at 24 and
        // 256x512 at 32 for the same face and character set. Choosing a square first and trimming
        // the height afterwards -- what this did before -- transposes three of those five
        // (plans/plan_xnapipeline_parity.md XNAPP-182).
        //
        std::uint32_t widestGlyph = 1u;
        for (const RasterGlyph& glyph : glyphs)
        {
            widestGlyph = std::max(widestGlyph, glyph.width + 1u);
        }

        // Every width the glyphs could go in, keeping the one that wastes least. Area first, then
        // the squarer of two sheets of equal area, then the wider: a 16x1024 sheet holds the same
        // 95 glyphs as a 128x128 one and is a poor texture on every renderer, so "smallest" alone
        // is not the whole rule. XNA's own atlases are all within a factor of two of square.
        const auto squareness = [](const std::uint32_t width, const std::uint32_t height)
        {
            std::uint32_t larger = std::max(width, height);
            std::uint32_t smaller = std::min(width, height);
            std::uint32_t steps = 0u;
            while (smaller < larger) { smaller <<= 1; ++steps; }
            return steps;
        };
        std::vector<PackedGlyph> placements;
        std::vector<PackedGlyph> candidatePlacements;
        std::uint32_t side = 0u;
        std::uint32_t usedHeight = 0u;
        std::uint64_t bestArea = 0u;
        std::uint32_t bestSquareness = 0u;
        for (std::uint32_t candidate = std::max(16u, RoundUpToPowerOfTwo(widestGlyph));
             candidate <= kMaximumAtlasSide; candidate <<= 1)
        {
            std::uint32_t candidateHeight = 0u;
            if (!PackGlyphs(glyphs, candidate, kMaximumAtlasSide, candidatePlacements,
                            candidateHeight))
            {
                continue;
            }
            const std::uint32_t rounded = RoundUpToPowerOfTwo(candidateHeight);
            const std::uint64_t area = static_cast<std::uint64_t>(candidate) * rounded;
            const std::uint32_t shape = squareness(candidate, rounded);
            if (side != 0u &&
                (area > bestArea ||
                 (area == bestArea && shape >= bestSquareness)))
            {
                continue;
            }
            side = candidate;
            usedHeight = candidateHeight;
            bestArea = area;
            bestSquareness = shape;
            placements = candidatePlacements;
        }
        if (side == 0u)
        {
            throw std::runtime_error(
                "'" + origin + "' at size " + std::to_string(description.size) + " needs " +
                std::to_string(glyphs.size()) +
                " glyphs, which do not fit in the maximum " +
                std::to_string(kMaximumAtlasSide) + "x" + std::to_string(kMaximumAtlasSide) +
                " glyph atlas. Reduce <Size> or narrow <CharacterRegions>.");
        }
        // The atlas is only as tall as it needs to be, rounded to a power of two: a font that
        // fills two shelves should not carry a mostly-empty square. Not capped at the width any
        // more -- a tall sheet is what a large font produces, and XNA's are 128x256 and 256x512.
        const std::uint32_t atlasHeight = RoundUpToPowerOfTwo(usedHeight);

        Cnb::CnbSpriteFontData font;
        font.atlas.width = side;
        font.atlas.height = atlasHeight;
        font.atlas.depth = 1u;
        font.atlas.faceCount = 1u;
        font.atlas.mipCount = 1u;
        Cnb::CnbTextureRepresentation representation;
        representation.format = Cnb::CnbTextureFormat::Rgba8;
        representation.levels.emplace_back(
            static_cast<std::size_t>(side) * atlasHeight * 4u, 0u);
        std::vector<std::uint8_t>& pixels = representation.levels.front();

        const int ascent = static_cast<int>(face.Handle()->size->metrics.ascender >> 6);
        const int descent = -static_cast<int>(face.Handle()->size->metrics.descender >> 6);

        // The line a SpriteFont advances by is the face's own line height -- ascender minus
        // descender plus line gap, scaled to the requested size -- rounded *up*. FreeType's
        // `metrics.height` is the same quantity rounded to nearest, which is a pixel short
        // whenever the fraction is below a half, so the unrounded product is taken and ceiled
        // here instead.
        //
        // Measured against XNA over eleven builds: five sizes of Liberation Mono and six of
        // Courier New, Arial and Georgia, which is what it took to be sure. Two rules fit
        // Liberation Mono's five sizes -- this one, and the sum of the separately rounded ascent
        // and descent -- and they disagree on Arial, whose line height is 21.465 px at size 14
        // where its rounded ascent and descent sum to 21 and XNA answers 22
        // (plans/plan_xnapipeline_parity.md XNAPP-182).
        const int lineHeight26_6 = static_cast<int>(
            FT_MulFix(face.Handle()->height, face.Handle()->size->metrics.y_scale));
        const int lineSpacing = (lineHeight26_6 + 63) >> 6;

        // The cropping rectangle's height is what SpriteFont.MeasureString() reports as the line's
        // height when no glyph is taller, so it is the line itself. XNA's is between one and five
        // pixels *more* than its own line spacing, by an amount no metric in the face predicts:
        // Courier New and Liberation Mono are metrically identical and answer 41 at size 24 where
        // Arial, whose line height is larger, answers 40. Recorded rather than fitted -- a formula
        // that matched one face and broke on the next is what the eleven measurements above were
        // for (plans/plan_xnapipeline_parity.md XNAPP-182, decisions.json `font_*`).
        const int cellHeight = lineSpacing;

        font.lineSpacing = lineSpacing;
        font.spacing = description.spacing;
        font.characters = characters;
        font.defaultCharacter = description.defaultCharacter;
        font.glyphBounds.reserve(glyphs.size());
        font.cropping.reserve(glyphs.size());
        font.kerning.reserve(glyphs.size());

        for (std::size_t index = 0u; index < glyphs.size(); ++index)
        {
            const RasterGlyph& glyph = glyphs[index];
            const PackedGlyph& placement = placements[index];
            for (std::uint32_t row = 0u; row < glyph.height; ++row)
            {
                for (std::uint32_t column = 0u; column < glyph.width; ++column)
                {
                    const std::uint8_t coverage =
                        glyph.coverage[static_cast<std::size_t>(row) * glyph.width + column];
                    const std::size_t offset =
                        ((static_cast<std::size_t>(placement.y + row) * side) +
                         placement.x + column) * 4u;
                    // Premultiplied white: SpriteBatch's default blend state is
                    // AlphaBlend, which expects premultiplied colour, and a glyph is white.
                    pixels[offset + 0u] = coverage;
                    pixels[offset + 1u] = coverage;
                    pixels[offset + 2u] = coverage;
                    pixels[offset + 3u] = coverage;
                }
            }

            font.glyphBounds.emplace_back(static_cast<int>(placement.x),
                                          static_cast<int>(placement.y),
                                          static_cast<int>(glyph.width),
                                          static_cast<int>(glyph.height));

            // The three kerning values are the ABC widths SpriteBatch::DrawString() advances by:
            // A is the left side bearing, B the ink width, C whatever the advance has left.
            //
            // <UseKerning>false</UseKerning> drops the two bearings and keeps the ink width alone,
            // so the line advances by the glyph rather than by the font's own metrics: measured on
            // font_spacing.spritefont, where XNA answers (0, 3, 0) for '!' against the kerned
            // (4, 3, 4) and leaves the cropping rectangle's X at zero. This pipeline used to fold
            // the whole advance into B and shift the ink with the cropping rectangle, which keeps
            // the text at its metric width -- reasonable, and not what XNA does, so a description
            // asking for tight text got loose text (plans/plan_xnapipeline_parity.md XNAPP-182).
            const int inkWidth = glyph.blank ? 1 : static_cast<int>(glyph.width);
            const int croppingX = 0;
            float a = 0.0f;
            const float b = static_cast<float>(inkWidth);
            float c = 0.0f;
            if (description.useKerning)
            {
                a = static_cast<float>(glyph.leftBearing);
                c = static_cast<float>(glyph.advance - glyph.leftBearing - inkWidth);
            }
            font.kerning.emplace_back(a, b, c);
            font.cropping.emplace_back(croppingX,
                                       glyph.blank ? 0 : ascent - glyph.topBearing,
                                       inkWidth, cellHeight);
        }

        font.atlas.representations.push_back(std::move(representation));
        return font;
    }
#else
    Cnb::CnbSpriteFontData RasterizeFontDescription(const FontDescription&,
                                                    std::vector<std::string>&,
                                                    const ContentStrictness)
    {
        throw std::runtime_error(
            "this build has no font rasterizer, so a .spritefont cannot be compiled. Configure "
            "with -DCNA_ENABLE_FONT_PIPELINE=ON and install FreeType development headers.");
    }
#endif
}

namespace CNA::Content::Pipeline
{
    namespace
    {
        /** @brief Directories searched when a description opts in to system fonts. */
        [[nodiscard]] std::vector<std::filesystem::path> SystemFontDirectories()
        {
            return {
                "/usr/share/fonts", "/usr/local/share/fonts",
                "C:/Windows/Fonts", "/Library/Fonts", "/System/Library/Fonts",
            };
        }

        /** @brief The file extensions a font file may carry. */
        [[nodiscard]] const std::vector<std::string>& FontExtensions()
        {
            static const std::vector<std::string> extensions{".ttf", ".otf", ".ttc", ".TTF",
                                                             ".OTF", ".TTC"};
            return extensions;
        }

        /**
         * @brief Searches the system font directories for a file whose stem matches a name.
         *
         * Deliberately a filename match rather than a family-name lookup: reading a font's
         * internal family table would need the rasterizer, which an unconfigured build does not
         * have, and a lookup that only sometimes works is worse than one whose rule is stated.
         *
         * @param fontName The authored `<FontName>`.
         * @return The first matching path in a deterministic walk, or an empty path.
         */
        [[nodiscard]] std::filesystem::path FindSystemFont(const std::string& fontName)
        {
            // The exported spelling below forwards here, so the XNA façade resolves a font the
            // same way the canonical importer does rather than inventing a second rule.
            std::string wanted;
            for (const char character : fontName)
            {
                if (character != ' ' && character != '-' && character != '_')
                {
                    wanted += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(character)));
                }
            }
            std::vector<std::filesystem::path> candidates;
            for (const std::filesystem::path& directory : SystemFontDirectories())
            {
                std::error_code error;
                if (!std::filesystem::is_directory(directory, error)) { continue; }
                for (std::filesystem::recursive_directory_iterator
                         entry(directory, std::filesystem::directory_options::skip_permission_denied,
                               error), end;
                     entry != end; entry.increment(error))
                {
                    if (error) { break; }
                    if (!entry->is_regular_file(error)) { continue; }
                    std::string stem = entry->path().stem().string();
                    std::string normalized;
                    for (const char character : stem)
                    {
                        if (character != ' ' && character != '-' && character != '_')
                        {
                            normalized += static_cast<char>(
                                std::tolower(static_cast<unsigned char>(character)));
                        }
                    }
                    if (normalized != wanted) { continue; }
                    const std::string extension = entry->path().extension().string();
                    const std::vector<std::string>& allowed = FontExtensions();
                    if (std::find(allowed.begin(), allowed.end(), extension) == allowed.end())
                    {
                        continue;
                    }
                    candidates.push_back(entry->path());
                }
            }
            // A deterministic build cannot depend on directory-iteration order.
            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? std::filesystem::path{} : candidates.front();
        }
    }

    ContentComponentIdentity FontDescriptionImporter::Identity() const
    {
        return {kImporterName, "1"};
    }

    std::vector<std::string> FontDescriptionImporter::SourceExtensions() const
    {
        return {".spritefont"};
    }

    std::filesystem::path FindSystemFontFile(const std::string& fontName)
    {
        return FindSystemFont(fontName);
    }

    std::vector<std::string> FontDescriptionImporter::OutputTypes() const
    {
        return {ImportedFontDescriptionType};
    }

    ContentValue FontDescriptionImporter::Import(ContentImporterContext& context) const
    {
        const std::filesystem::path& source = context.SourcePath();
        const std::string origin = CNA::Internal::ContentPathToUtf8(source);
        std::ifstream stream(source, std::ios::binary);
        if (!stream) { Fail(origin, "cannot be opened"); }
        const std::string xml{std::istreambuf_iterator<char>(stream),
                              std::istreambuf_iterator<char>()};

        FontDescription description = ParseFontDescription(xml, origin);

        // A project-relative font file is resolved first, and recorded as a build dependency so
        // replacing the .ttf rebuilds the font. This is the reproducible route and the default.
        std::vector<std::string> attempted;
        const auto tryProjectFile = [&](const std::string& candidate) -> bool
        {
            // Existence is checked before the dependency is recorded: ResolveSourceDependency()
            // both validates containment and enrols the path in the build's byte-hashed input
            // set, and enrolling a path that does not exist would fail the whole build while
            // merely probing for the right extension.
            std::error_code error;
            const std::filesystem::path probe =
                context.SourcePath().parent_path() / CNA::Internal::ContentPathFromUtf8(candidate);
            if (std::filesystem::is_regular_file(probe, error))
            {
                try
                {
                    description.resolvedFontFile = context.ResolveSourceDependency(candidate);
                    return true;
                }
                catch (const std::invalid_argument&)
                {
                    // An absolute or escaping spelling is not a candidate; the aggregate
                    // diagnostic below names everything that was tried.
                }
            }
            attempted.push_back(candidate);
            return false;
        };

        bool resolved = tryProjectFile(description.fontName);
        if (!resolved)
        {
            for (const std::string& extension : FontExtensions())
            {
                if (extension != ".ttf" && extension != ".otf" && extension != ".ttc")
                {
                    continue;
                }
                if (tryProjectFile(description.fontName + extension))
                {
                    resolved = true;
                    break;
                }
            }
        }

        if (!resolved)
        {
            const std::filesystem::path systemFont = FindSystemFont(description.fontName);
            if (!systemFont.empty())
            {
                description.resolvedFontFile = systemFont;
                description.resolvedFromSystemFonts = true;
                resolved = true;
                context.LogWarning(
                    "<FontName> '" + description.fontName +
                    "' was resolved to the installed font '" +
                    CNA::Internal::ContentPathToUtf8(systemFont) +
                    "'. That makes this build depend on what is installed on this machine; put "
                    "the font file beside the .spritefont and name it there for a reproducible "
                    "build.");
            }
        }

        if (!resolved)
        {
            std::string tried;
            for (const std::string& candidate : attempted)
            {
                if (!tried.empty()) { tried += ", "; }
                tried += "'" + candidate + "'";
            }
            Fail(origin,
                 "<FontName> '" + description.fontName +
                     "' names no font file beside the description (tried " + tried +
                     ") and no installed font of that name was found. XNA resolves a font family "
                     "through Windows; CNA resolves a file, so that a content build produces the "
                     "same bytes on every machine");
        }

        return ContentValue::Create(ImportedFontDescriptionType, std::move(description));
    }

    ContentComponentIdentity FontDescriptionProcessor::Identity() const
    {
        return {kProcessorName, "1"};
    }

    std::string FontDescriptionProcessor::InputType() const
    {
        return ImportedFontDescriptionType;
    }

    std::string FontDescriptionProcessor::OutputType() const { return ProcessedSpriteFontType; }

    void FontDescriptionProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            throw ContentParameterError(
                ContentParameterFault::UnknownName, name,
                "FontDescriptionProcessor does not recognize parameter '" + name +
                    "'; a .spritefont carries its own complete policy.");
        }
    }

    namespace
    {
        /**
         * @brief Block-compresses a glyph atlas when the build is producing an `.xnb`.
         *
         * XNA's own `FontDescriptionProcessor` hands the writer a DXT3 atlas: measured on four
         * descriptions, at two sizes, and the format is `Dxt3` in every one
         * (`tests/reference/xna40/differential/font_description*.xnb`,
         * plans/plan_xnapipeline_parity.md XNAPP-182). DXT3 rather than DXT1 or DXT5 because a
         * glyph is a coverage mask and DXT3's four explicit alpha bits per texel are what a mask
         * wants; DXT5's interpolated alpha would band it. The size difference is not marginal:
         * 16 KB against 128 KB for the same 95-glyph font.
         *
         * Only for `.xnb`. A `.cnb` keeps the lossless 8-bit atlas, because that container is
         * CNA's own and nothing about it obliges a 2010 memory budget. An atlas whose side is not
         * a multiple of four is left uncompressed, as block compression cannot take it -- the
         * packer rounds to powers of two, so that is a case the corpus does not have and a
         * refusal here would fail a build for a rule it could simply not apply.
         *
         * @param font The rasterized font, whose atlas is replaced in place.
         * @param context The build, which says which container is being produced.
         */
        void CompressAtlasForXnb(Cnb::CnbSpriteFontData& font, ContentProcessorContext& context)
        {
            if (context.OutputFormat() != ContentOutputFormat::Xnb) { return; }
            if (font.atlas.representations.empty()) { return; }
            Cnb::CnbTextureRepresentation& atlas = font.atlas.representations.front();
            if (atlas.format != Cnb::CnbTextureFormat::Rgba8 || atlas.levels.empty()) { return; }
            if ((font.atlas.width % 4u) != 0u || (font.atlas.height % 4u) != 0u) { return; }

            static const TextureBlockEncoder encoder = MakeBlockCompressionTextureEncoder();
            atlas.levels.front() = encoder(Cnb::CnbTextureFormat::Bc2, atlas.levels.front(),
                                           font.atlas.width, font.atlas.height);
            atlas.format = Cnb::CnbTextureFormat::Bc2;
            context.LogInfo("compressed the glyph atlas to DXT3, as XNA's own font processor does.");
        }
    }

    ContentValue FontDescriptionProcessor::Process(const ContentValue& input,
                                                   ContentProcessorContext& context) const
    {
        const FontDescription& description = input.Get<FontDescription>();
        std::vector<std::string> warnings;
        Cnb::CnbSpriteFontData font =
            RasterizeFontDescription(description, warnings, context.Environment().strictness);
        for (const std::string& warning : warnings) { context.LogWarning(warning); }
        context.LogInfo("rasterized " + std::to_string(font.characters.size()) +
                        " glyph(s) into a " + std::to_string(font.atlas.width) + "x" +
                        std::to_string(font.atlas.height) + " atlas.");
        CompressAtlasForXnb(font, context);
        return ContentValue::Create(ProcessedSpriteFontType, std::move(font));
    }

    void RegisterSpriteFontSourceContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<const FontDescriptionImporter>());
        registry.RegisterProcessor(std::make_shared<const FontDescriptionProcessor>());
    }
}
