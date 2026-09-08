// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <variant>

#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/TextureCompressionPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Xml.hpp"

#if defined(CNA_HAVE_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#endif

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kImporterName = "CNA.FontDescriptionImporter";
        constexpr const char* kProcessorName = "CNA.FontDescriptionProcessor";
        constexpr const char* kFontTextureProcessorName = "CNA.FontTextureProcessor";

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
                                       std::uint32_t& usedHeight, const std::uint32_t kGap)
        {
            constexpr std::uint32_t kMargin = 1u;
            placements.assign(glyphs.size(), PackedGlyph{});
            // Tallest first, then widest. A shelf is as tall as its tallest member, so packing in
            // character order lets one accented capital raise the shelf every other glyph on it
            // sits in; ordering by height keeps each shelf close to the height of what is on it,
            // and the width tie-break is XNA's own. The glyphs themselves are not reordered --
            // only the order they are placed in -- so a glyph's rectangle still belongs to its own
            // character.
            std::vector<std::size_t> order(glyphs.size());
            for (std::size_t index = 0u; index < order.size(); ++index) { order[index] = index; }
            std::stable_sort(order.begin(), order.end(),
                             [&glyphs](const std::size_t left, const std::size_t right)
                             {
                                 if (glyphs[left].height != glyphs[right].height)
                                 {
                                     return glyphs[left].height > glyphs[right].height;
                                 }
                                 // Widest first among equals, which is XNA's own tie-break:
                                 // a sheet of 4x6, 7x6 and 3x6 cells comes out at (1,1), (10,1)
                                 // and (1,9) there, and that is the widest, then the next, then
                                 // the wrap (plans/plan_xnapipeline_parity.md XNAPP-139).
                                 return glyphs[left].width > glyphs[right].width;
                             });

            // Each glyph goes at the lowest row it fits in and the leftmost column of that row,
            // not on a shelf. The two are not the same packer and the difference is visible in
            // every real font: a shelf is as tall as its tallest member and leaves the space under
            // a short glyph unused, where this drops the next glyph into it. Measured against
            // XNA's own output -- this reproduces **every** placement of SAMPLE-062's
            // `NetRumbleFont` (95 of 95) and of the four sheets in the differential corpus (3, 10,
            // 3 and 2 of them), where the shelf packer matched only the small ones
            // (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-131).
            //
            // A candidate row is `kMargin` or the bottom of something already placed, and a
            // candidate column is `kMargin` or the right edge of something already placed: a
            // position that is free while the one a texel above or to its left is not can only be
            // one of those, so scanning them is scanning every position that can win.
            struct Placed
            {
                std::uint32_t x = 0u;
                std::uint32_t y = 0u;
                std::uint32_t width = 0u;
                std::uint32_t height = 0u;
            };
            std::vector<Placed> placed;
            placed.reserve(glyphs.size());
            std::uint32_t usedBottom = kMargin;
            for (const std::size_t index : order)
            {
                const RasterGlyph& glyph = glyphs[index];
                if (glyph.width + 2u * kMargin > width ||
                    glyph.height + 2u * kMargin > maximumHeight)
                {
                    return false;
                }
                std::vector<std::uint32_t> rows{kMargin};
                for (const Placed& one : placed) { rows.push_back(one.y + one.height + kGap); }
                std::sort(rows.begin(), rows.end());
                rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

                bool found = false;
                std::uint32_t chosenX = 0u;
                std::uint32_t chosenY = 0u;
                for (const std::uint32_t row : rows)
                {
                    if (row + glyph.height + kMargin > maximumHeight) { continue; }
                    std::vector<std::uint32_t> columns{kMargin};
                    for (const Placed& one : placed) { columns.push_back(one.x + one.width + kGap); }
                    std::sort(columns.begin(), columns.end());
                    columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
                    for (const std::uint32_t column : columns)
                    {
                        if (column + glyph.width + kMargin > width) { continue; }
                        const auto clash = [&](const Placed& one)
                        {
                            return column < one.x + one.width + kGap &&
                                   one.x < column + glyph.width + kGap &&
                                   row < one.y + one.height + kGap &&
                                   one.y < row + glyph.height + kGap;
                        };
                        if (std::any_of(placed.begin(), placed.end(), clash)) { continue; }
                        chosenX = column;
                        chosenY = row;
                        found = true;
                        break;
                    }
                    if (found) { break; }
                }
                if (!found) { return false; }
                placements[index] = {chosenX, chosenY};
                placed.push_back({chosenX, chosenY, glyph.width, glyph.height});
                usedBottom = std::max(usedBottom, chosenY + glyph.height + kMargin);
            }
            usedHeight = usedBottom;
            return true;
        }

        /**
         * @brief Chooses the atlas width that wastes least, then packs into it.
         *
         * Every width the glyphs could go in, keeping the one that wastes least. Area first, then
         * the squarer of two sheets of equal area, then the wider: a 16x1024 sheet holds the same
         * 95 glyphs as a 128x128 one and is a poor texture on every renderer, so "smallest" alone
         * is not the whole rule. XNA's own atlases are all within a factor of two of square, and
         * none is narrower than 16.
         *
         * @param glyphs The glyphs to pack.
         * @param side Receives the chosen width.
         * @param usedHeight Receives the rows actually occupied.
         * @param placements Receives one placement per glyph.
         * @param gap Texels between two glyphs. One for the description route, whose packer is
         *        CNA's own and whose atlas is a recorded divergence either way; **two** for a font
         *        sheet, where it is measured -- three 5x7 glyphs come out at (1,1), (8,1) and
         *        (1,10) in a 16x32 atlas, which is a one-texel margin and a two-texel gap and no
         *        other rule (plans/plan_xnapipeline_parity.md XNAPP-139).
         * @return False when the glyphs do not fit any allowed atlas.
         */
        [[nodiscard]] bool ChooseAtlas(const std::vector<RasterGlyph>& glyphs, std::uint32_t& side,
                                       std::uint32_t& usedHeight,
                                       std::vector<PackedGlyph>& placements,
                                       const std::uint32_t gap)
        {
            const auto squareness = [](const std::uint32_t width, const std::uint32_t height)
            {
                std::uint32_t larger = std::max(width, height);
                std::uint32_t smaller = std::min(width, height);
                std::uint32_t steps = 0u;
                while (smaller < larger) { smaller <<= 1; ++steps; }
                return steps;
            };
            std::uint32_t widestGlyph = 1u;
            for (const RasterGlyph& glyph : glyphs)
            {
                widestGlyph = std::max(widestGlyph, glyph.width + 1u);
            }

            std::vector<PackedGlyph> candidatePlacements;
            side = 0u;
            usedHeight = 0u;
            std::uint64_t bestArea = 0u;
            std::uint32_t bestSquareness = 0u;
            for (std::uint32_t candidate = std::max(16u, RoundUpToPowerOfTwo(widestGlyph));
                 candidate <= kMaximumAtlasSide; candidate <<= 1)
            {
                std::uint32_t candidateHeight = 0u;
                if (!PackGlyphs(glyphs, candidate, kMaximumAtlasSide, candidatePlacements,
                                candidateHeight, gap))
                {
                    continue;
                }
                const std::uint32_t rounded = RoundUpToPowerOfTwo(candidateHeight);
                const std::uint64_t area = static_cast<std::uint64_t>(candidate) * rounded;
                const std::uint32_t shape = squareness(candidate, rounded);
                if (side != 0u && (area > bestArea ||
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
            return side != 0u;
        }
    }

    Cnb::CnbSpriteFontData RasterizeFontDescription(
        const FontDescription& description, std::vector<std::string>& warnings,
        const ContentStrictness strictness,
        const Microsoft::Xna::Framework::Graphics::GraphicsProfile profile)
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
        std::vector<PackedGlyph> placements;
        std::uint32_t side = 0u;
        std::uint32_t usedHeight = 0u;
        if (!ChooseAtlas(glyphs, side, usedHeight, placements, 1u))
        {
            throw std::runtime_error(
                "'" + origin + "' at size " + std::to_string(description.size) + " needs " +
                std::to_string(glyphs.size()) +
                " glyphs, which do not fit in the maximum " +
                std::to_string(kMaximumAtlasSide) + "x" + std::to_string(kMaximumAtlasSide) +
                " glyph atlas. Reduce <Size> or narrow <CharacterRegions>.");
        }
        // How tall the sheet is, is the profile's rule and not the packer's. Measured over the
        // 196 distinct sprite-font atlases the genuine pipeline produced for the public XNA
        // samples: of the 186 whose two candidate answers differ, **every** Reach atlas is the
        // packed height rounded up to a power of two and **every** HiDef one is it rounded up to
        // four -- 119 and 67, with no exception either way. Rounding to a power of two whatever
        // the profile is what CNA did, and it costs a Reach-shaped sheet on a HiDef target: the
        // CardsStarterKit sample's three fonts are 256x144, 256x196 and 256x120 there against a
        // 256x256 here, which is 70,830 bytes of `.xnb` where XNA writes 42,158
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-104`).
        const std::uint32_t atlasHeight =
            profile == Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef
                ? ((usedHeight + 3u) & ~3u)
                : RoundUpToPowerOfTwo(usedHeight);

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
    Cnb::CnbSpriteFontData RasterizeFontDescription(
        const FontDescription&, std::vector<std::string>&, const ContentStrictness,
        const Microsoft::Xna::Framework::Graphics::GraphicsProfile)
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
        /** @brief Case, space, hyphen and underscore removed; the key both lookups compare on. */
        [[nodiscard]] std::string NormalizeFontName(const std::string& text)
        {
            std::string normalized;
            for (const char character : text)
            {
                if (character != ' ' && character != '-' && character != '_')
                {
                    normalized += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(character)));
                }
            }
            return normalized;
        }

        /** @brief The directories a build may add to the search, in the order it named them. */
        [[nodiscard]] std::vector<std::filesystem::path>& ConfiguredFontDirectories()
        {
            static std::vector<std::filesystem::path> directories;
            return directories;
        }

        /**
         * @brief Directories searched when a description opts in to system fonts.
         *
         * The directories a build configured come first, then `CNA_FONT_PATH` from the
         * environment, then the platform's own. A game whose fonts are the XNA redistributable
         * pack rather than anything installed has no other way to be built here, and installing
         * them machine-wide is not something a content build may ask of its host.
         */
        [[nodiscard]] std::vector<std::filesystem::path> SystemFontDirectories(
            std::size_t* configuredCount = nullptr)
        {
            std::vector<std::filesystem::path> directories = ConfiguredFontDirectories();
            if (const char* const configured = std::getenv("CNA_FONT_PATH"); configured != nullptr)
            {
#if defined(_WIN32)
                constexpr char kSeparator = ';';
#else
                constexpr char kSeparator = ':';
#endif
                std::string text(configured);
                std::size_t start = 0u;
                while (start <= text.size())
                {
                    const std::size_t end = text.find(kSeparator, start);
                    const std::string piece =
                        text.substr(start, end == std::string::npos ? std::string::npos
                                                                    : end - start);
                    if (!piece.empty())
                    {
                        directories.emplace_back(CNA::Internal::ContentPathFromUtf8(piece));
                    }
                    if (end == std::string::npos) { break; }
                    start = end + 1u;
                }
            }
            if (configuredCount != nullptr) { *configuredCount = directories.size(); }
            for (const char* const builtin : {"/usr/share/fonts", "/usr/local/share/fonts",
                                              "C:/Windows/Fonts", "/Library/Fonts",
                                              "/System/Library/Fonts"})
            {
                directories.emplace_back(builtin);
            }
            return directories;
        }

        /** @brief The file extensions a font file may carry. */
        [[nodiscard]] const std::vector<std::string>& FontExtensions()
        {
            static const std::vector<std::string> extensions{".ttf", ".otf", ".ttc", ".TTF",
                                                             ".OTF", ".TTC"};
            return extensions;
        }

        /** @brief Every font file under one directory, in a deterministic order. */
        [[nodiscard]] std::vector<std::filesystem::path> FontFilesUnder(
            const std::filesystem::path& directory, const bool recursive)
        {
            std::vector<std::filesystem::path> files;
            std::error_code error;
            if (!std::filesystem::is_directory(directory, error)) { return files; }
            const auto consider = [&files](const std::filesystem::path& path)
            {
                const std::string extension = path.extension().string();
                const std::vector<std::string>& allowed = FontExtensions();
                if (std::find(allowed.begin(), allowed.end(), extension) != allowed.end())
                {
                    files.push_back(path);
                }
            };
            if (recursive)
            {
                for (std::filesystem::recursive_directory_iterator
                         entry(directory, std::filesystem::directory_options::skip_permission_denied,
                               error), end;
                     entry != end; entry.increment(error))
                {
                    if (error) { break; }
                    if (entry->is_regular_file(error)) { consider(entry->path()); }
                }
            }
            else
            {
                for (std::filesystem::directory_iterator
                         entry(directory, std::filesystem::directory_options::skip_permission_denied,
                               error), end;
                     entry != end; entry.increment(error))
                {
                    if (error) { break; }
                    if (entry->is_regular_file(error)) { consider(entry->path()); }
                }
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        /** @brief One face of one font file: what a family lookup has to compare and choose on. */
        struct FontFaceIdentity
        {
            std::filesystem::path path;
            long index = 0;
            std::string family;
            bool bold = false;
            bool italic = false;
        };

#if defined(CNA_HAVE_FREETYPE)
        /**
         * @brief One `name` table entry of a face, as UTF-8.
         *
         * @param face The opened face.
         * @param nameId The `name` table identifier: 1 is the family, 2 the subfamily.
         * @return The entry, or empty when the face has none this can read.
         */
        [[nodiscard]] std::string SfntName(const FT_Face face, const FT_UShort nameId)
        {
            std::string best;
            int bestRank = -1;
            const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
            for (FT_UInt index = 0; index < count; ++index)
            {
                FT_SfntName entry{};
                if (FT_Get_Sfnt_Name(face, index, &entry) != 0) { continue; }
                if (entry.name_id != nameId) { continue; }
                int rank = -1;
                std::string text;
                if (entry.platform_id == TT_PLATFORM_MICROSOFT)
                {
                    // UTF-16BE. English (0x0409) first, then any other Windows language.
                    rank = entry.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES ? 3 : 2;
                    for (FT_UInt at = 0; at + 1u < entry.string_len; at += 2u)
                    {
                        const unsigned code = static_cast<unsigned>(entry.string[at]) << 8 |
                                              entry.string[at + 1u];
                        // The name table's family and subfamily are ASCII in every font this has
                        // to read; anything above is left out rather than mangled.
                        if (code != 0u && code < 0x80u) { text += static_cast<char>(code); }
                    }
                }
                else if (entry.platform_id == TT_PLATFORM_MACINTOSH)
                {
                    rank = 1;
                    for (FT_UInt at = 0; at < entry.string_len; ++at)
                    {
                        const unsigned char code = entry.string[at];
                        if (code != 0u && code < 0x80u) { text += static_cast<char>(code); }
                    }
                }
                if (rank > bestRank && !text.empty())
                {
                    best = text;
                    bestRank = rank;
                }
            }
            return best;
        }
#endif

        /**
         * @brief Every face of one font file, with the family name it declares.
         *
         * The family is the `name` table's entry 1 -- the one Windows matches a `LOGFONT` against,
         * and therefore the one `<FontName>` names. It is read directly rather than taken from
         * FreeType's own `family_name`, which prefers the *typographic* family (entry 16) where a
         * font has one: `Moire-ExtraBold.ttf` declares family 'Moire ExtraBold' and typographic
         * family 'Moire', so a lookup through `family_name` answers 'Moire' with the extra-bold
         * face and the CardsStarterKit sample's regular font comes out extra bold
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-105`).
         *
         * Empty where this build has no rasterizer, which is what makes the family lookup an
         * addition to the file-name one rather than a replacement for it.
         */
        [[nodiscard]] std::vector<FontFaceIdentity> FacesOf(const std::filesystem::path& path)
        {
            std::vector<FontFaceIdentity> faces;
#if defined(CNA_HAVE_FREETYPE)
            FT_Library library = nullptr;
            if (FT_Init_FreeType(&library) != 0) { return faces; }
            const std::string native = CNA::Internal::ContentPathToUtf8(path);
            long count = 1;
            for (long index = 0; index < count; ++index)
            {
                FT_Face face = nullptr;
                if (FT_New_Face(library, native.c_str(), index, &face) != 0) { break; }
                count = face->num_faces > 0 ? face->num_faces : 1;
                std::string family = SfntName(face, 1u);
                if (family.empty() && face->family_name != nullptr) { family = face->family_name; }
                if (!family.empty())
                {
                    faces.push_back({path, index, family,
                                     (face->style_flags & FT_STYLE_FLAG_BOLD) != 0,
                                     (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0});
                }
                FT_Done_Face(face);
            }
            FT_Done_FreeType(library);
#else
            (void)path;
#endif
            return faces;
        }

        /**
         * @brief The face of a family that best answers a requested style.
         *
         * XNA asks Windows for a family and a style and gets the real styled face; the emphasis is
         * synthesized only where the family has not got one. Exact match first, then the regular
         * face, then whatever the family has, so that the answer never depends on the order a
         * directory happened to be walked in.
         *
         * @param faces Candidate faces, already restricted to one family.
         * @param style The `<Style>` the description asked for.
         * @return The chosen path, or empty when there are no candidates.
         */
        [[nodiscard]] std::filesystem::path SelectStyledFace(std::vector<FontFaceIdentity> faces,
                                                             const FontDescriptionStyle style)
        {
            if (faces.empty()) { return {}; }
            std::sort(faces.begin(), faces.end(),
                      [](const FontFaceIdentity& left, const FontFaceIdentity& right)
                      {
                          return std::tie(left.path, left.index) <
                                 std::tie(right.path, right.index);
                      });
            const bool wantsBold = style == FontDescriptionStyle::Bold ||
                                   style == FontDescriptionStyle::BoldItalic;
            const bool wantsItalic = style == FontDescriptionStyle::Italic ||
                                     style == FontDescriptionStyle::BoldItalic;
            for (const FontFaceIdentity& face : faces)
            {
                if (face.bold == wantsBold && face.italic == wantsItalic) { return face.path; }
            }
            for (const FontFaceIdentity& face : faces)
            {
                if (!face.bold && !face.italic) { return face.path; }
            }
            return faces.front().path;
        }

        /**
         * @brief Looks for a font family among a set of font files.
         *
         * `<FontName>` is a font *family* name: XNA resolves it through Windows, and the families
         * the public samples name -- Pericles, Segoe UI Mono, Moire ExtraBold, Kootenay, Wasco
         * Sans -- live in files called `Peric.ttf`, `SegoeUIMono-Regular.ttf`,
         * `Moire-ExtraBold.ttf`, `kooten.ttf` and `wscsnrg.ttf`. A file-name match cannot find one
         * of them, which is why this exists beside it.
         *
         * @param files The font files to consider.
         * @param wanted The normalized family name.
         * @param style The requested style.
         * @return The chosen file, or empty.
         */
        [[nodiscard]] std::filesystem::path FindFontFamilyAmong(
            const std::vector<std::filesystem::path>& files, const std::string& wanted,
            const FontDescriptionStyle style)
        {
            std::vector<FontFaceIdentity> matches;
            for (const std::filesystem::path& file : files)
            {
                for (FontFaceIdentity& face : FacesOf(file))
                {
                    if (NormalizeFontName(face.family) == wanted)
                    {
                        matches.push_back(std::move(face));
                    }
                }
            }
            return SelectStyledFace(std::move(matches), style);
        }

        /**
         * @brief Searches the font directories for the family, then for a file of that name.
         *
         * The family lookup is XNA's own rule and comes first; the file-name match is CNA's, kept
         * because it is what a build with no rasterizer can still do and what a font file named
         * after its family answers to either way.
         *
         * @param fontName The authored `<FontName>`.
         * @param style The authored `<Style>`.
         * @return The chosen path in a deterministic walk, or an empty path.
         */
        [[nodiscard]] std::filesystem::path FindSystemFont(const std::string& fontName,
                                                           const FontDescriptionStyle style,
                                                           bool* fromConfiguredDirectory = nullptr)
        {
            // The exported spelling below forwards here, so the XNA façade resolves a font the
            // same way the canonical importer does rather than inventing a second rule.
            const std::string wanted = NormalizeFontName(fontName);
            std::size_t configured = 0u;
            std::vector<std::filesystem::path> files;
            std::size_t configuredFiles = 0u;
            const std::vector<std::filesystem::path> directories = SystemFontDirectories(&configured);
            for (std::size_t index = 0u; index < directories.size(); ++index)
            {
                for (std::filesystem::path& file : FontFilesUnder(directories[index], true))
                {
                    files.push_back(std::move(file));
                }
                if (index + 1u == configured) { configuredFiles = files.size(); }
            }
            const auto answer = [&](const std::filesystem::path& chosen)
            {
                if (fromConfiguredDirectory != nullptr)
                {
                    *fromConfiguredDirectory =
                        std::find(files.begin(), files.begin() + static_cast<std::ptrdiff_t>(
                                                     configuredFiles),
                                  chosen) != files.begin() + static_cast<std::ptrdiff_t>(
                                                 configuredFiles);
                }
                return chosen;
            };
            if (const std::filesystem::path family = FindFontFamilyAmong(files, wanted, style);
                !family.empty())
            {
                return answer(family);
            }
            std::vector<std::filesystem::path> candidates;
            for (const std::filesystem::path& file : files)
            {
                if (NormalizeFontName(file.stem().string()) == wanted)
                {
                    candidates.push_back(file);
                }
            }
            // A deterministic build cannot depend on directory-iteration order.
            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? std::filesystem::path{} : answer(candidates.front());
        }
    }

    ContentComponentIdentity FontDescriptionImporter::Identity() const
    {
        // The font search is part of the importer's version for the reason the effect route's
        // compiler is part of its processor's: the manifest fingerprints the component identity,
        // and the same description legitimately resolves to a different font file when the build
        // is told to look somewhere else. Without this, changing --font-directory left the
        // previous build's output in place as unchanged. An unconfigured build keeps the bare
        // version, so nothing that never used the option changes fingerprint
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-100`).
        const std::vector<std::filesystem::path>& directories = ConfiguredFontDirectories();
        if (directories.empty()) { return {kImporterName, "1"}; }
        std::string version = "1+fonts";
        for (const std::filesystem::path& directory : directories)
        {
            version += ":" + CNA::Internal::ContentPathToUtf8(directory);
        }
        return {kImporterName, version};
    }

    std::vector<std::string> FontDescriptionImporter::SourceExtensions() const
    {
        return {".spritefont"};
    }

    std::filesystem::path FindSystemFontFile(const std::string& fontName,
                                             const FontDescriptionStyle style)
    {
        return FindSystemFont(fontName, style);
    }

    std::filesystem::path FindFontFamilyBeside(const std::filesystem::path& directory,
                                               const std::string& fontName,
                                               const FontDescriptionStyle style)
    {
        return FindFontFamilyAmong(FontFilesUnder(directory, false), NormalizeFontName(fontName),
                                   style);
    }

    void SetFontSearchDirectoriesEXT(std::vector<std::filesystem::path> directories)
    {
        ConfiguredFontDirectories() = std::move(directories);
    }

    const std::vector<std::filesystem::path>& FontSearchDirectoriesEXT() noexcept
    {
        return ConfiguredFontDirectories();
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

        // A font file beside the description whose own family table answers `<FontName>` is still
        // the reproducible route -- `Peric.ttf` *is* the Pericles family -- so it is looked for
        // before anything installed on the machine.
        if (!resolved)
        {
            const std::filesystem::path beside = FindFontFamilyBeside(
                context.SourcePath().parent_path(), description.fontName, description.style);
            if (!beside.empty())
            {
                try
                {
                    description.resolvedFontFile = context.ResolveSourceDependency(
                        CNA::Internal::ContentPathToUtf8(beside.filename()));
                    resolved = true;
                }
                catch (const std::invalid_argument&)
                {
                    // Not a spelling this build may depend on; the search below still may find it.
                }
            }
        }

        if (!resolved)
        {
            bool configured = false;
            const std::filesystem::path systemFont =
                FindSystemFont(description.fontName, description.style, &configured);
            if (!systemFont.empty())
            {
                description.resolvedFontFile = systemFont;
                description.resolvedFromSystemFonts = true;
                resolved = true;
                // A directory this build was told to search is part of the build's own
                // description of itself and repeats on any machine that passes it; the
                // platform's own font directories are not, and that is what the warning is
                // about, so it says which one this was.
                context.LogWarning(
                    configured
                        ? "<FontName> '" + description.fontName + "' was resolved to '" +
                              CNA::Internal::ContentPathToUtf8(systemFont) +
                              "', in a font directory this build was told to search."
                        : "<FontName> '" + description.fontName +
                              "' was resolved to the installed font '" +
                              CNA::Internal::ContentPathToUtf8(systemFont) +
                              "'. That makes this build depend on what is installed on this "
                              "machine; put the font file beside the .spritefont and name it "
                              "there, or pass --font-directory, for a reproducible build.");
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
        Cnb::CnbSpriteFontData font = RasterizeFontDescription(
            description, warnings, context.Environment().strictness,
            context.Environment().targetProfile);
        for (const std::string& warning : warnings) { context.LogWarning(warning); }
        context.LogInfo("rasterized " + std::to_string(font.characters.size()) +
                        " glyph(s) into a " + std::to_string(font.atlas.width) + "x" +
                        std::to_string(font.atlas.height) + " atlas.");
        CompressAtlasForXnb(font, context);
        return ContentValue::Create(ProcessedSpriteFontType, std::move(font));
    }

    namespace
    {
        /**
         * @brief The colour a font sheet separates its glyphs with.
         *
         * Fixed at magenta rather than read from the sheet's own corner: a sheet bordered in
         * transparent black is refused with the same sentence an empty one gets, so XNA is not
         * taking the border colour from the image (measured, `fonttexture/sheet_alpha_border`).
         * Alpha is not part of the comparison, because a sheet may be authored with the separator
         * opaque or transparent and XNA reads both.
         */
        [[nodiscard]] bool IsSeparator(const std::uint8_t* texel) noexcept
        {
            return texel[0] == 255u && texel[1] == 0u && texel[2] == 255u;
        }

        /** @brief One glyph found in a sheet. */
        struct SheetGlyph
        {
            std::uint32_t x = 0u;
            std::uint32_t y = 0u;
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
        };

        /**
         * @brief Every glyph rectangle in a sheet, in reading order.
         *
         * Rows of glyphs are the bands of scanlines that hold anything but the separator, and the
         * glyphs in a band are the runs of columns that do. Measured against three genuine builds:
         * a sheet of three equal cells, one of three unequal ones and one of ten answered exactly
         * these rectangles, in this order.
         */
        [[nodiscard]] std::vector<SheetGlyph> FindSheetGlyphs(const std::uint32_t width,
                                                              const std::uint32_t height,
                                                              const std::vector<std::uint8_t>& rgba)
        {
            const auto texel = [&](const std::uint32_t x, const std::uint32_t y) {
                return (static_cast<std::size_t>(y) * width + x) * 4u;
            };

            // The sheet begins at its first magenta texel, and a sheet with none has no glyphs at
            // all. Both halves are measured. A 2x2 image of four ordinary colours is refused --
            // `Cannot build this font: there were no glyphs found to build` -- and so is a sheet
            // whose cells are separated by transparent black instead, which has perfectly good
            // glyphs in it; that is what says the separator colour is fixed rather than read from
            // the image. And a sheet whose *corner* texel is white while the rest of its border is
            // magenta still answers exactly its three glyphs, which is what says the scan starts at
            // the first magenta texel rather than at the origin (plans/plan_xnapipeline_parity.md
            // XNAPP-139).
            std::uint32_t originX = width;
            std::uint32_t originY = height;
            for (std::uint32_t y = 0u; y < height && originY == height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const std::size_t at = texel(x, y);
                    if (at + 3u < rgba.size() && IsSeparator(&rgba[at]))
                    {
                        originX = x;
                        originY = y;
                        break;
                    }
                }
            }
            if (originY == height) { return {}; }

            const auto occupied = [&](const std::uint32_t x, const std::uint32_t y) {
                const std::size_t at = texel(x, y);
                return at + 3u < rgba.size() && !IsSeparator(&rgba[at]);
            };

            std::vector<SheetGlyph> glyphs;
            std::uint32_t y = originY;
            while (y < height)
            {
                std::uint32_t bandTop = y;
                bool any = false;
                for (std::uint32_t x = originX; x < width && !any; ++x) { any = occupied(x, bandTop); }
                if (!any) { ++y; continue; }

                std::uint32_t bandBottom = bandTop;
                while (bandBottom + 1u < height)
                {
                    bool next = false;
                    for (std::uint32_t x = originX; x < width && !next; ++x)
                    {
                        next = occupied(x, bandBottom + 1u);
                    }
                    if (!next) { break; }
                    ++bandBottom;
                }

                std::uint32_t x = originX;
                while (x < width)
                {
                    bool column = false;
                    for (std::uint32_t row = bandTop; row <= bandBottom && !column; ++row)
                    {
                        column = occupied(x, row);
                    }
                    if (!column) { ++x; continue; }
                    std::uint32_t right = x;
                    while (right + 1u < width)
                    {
                        bool next = false;
                        for (std::uint32_t row = bandTop; row <= bandBottom && !next; ++row)
                        {
                            next = occupied(right + 1u, row);
                        }
                        if (!next) { break; }
                        ++right;
                    }
                    glyphs.push_back({x, bandTop, right - x + 1u, bandBottom - bandTop + 1u});
                    x = right + 1u;
                }
                y = bandBottom + 1u;
            }
            return glyphs;
        }
    }

    Cnb::CnbSpriteFontData BuildFontFromTextureSheet(const std::uint32_t width,
                                                     const std::uint32_t height,
                                                     const std::vector<std::uint8_t>& rgba,
                                                     const SharpRuntime::charcs firstCharacter,
                                                     const std::string& origin)
    {
        const std::vector<SheetGlyph> found = FindSheetGlyphs(width, height, rgba);
        if (found.empty())
        {
            // XNA's own sentence, measured from a build of a sheet with no glyph in it.
            Fail(origin, "Cannot build this font: there were no glyphs found to build");
        }

        // A cell is not a glyph: XNA trims each cell to the texels that are not the separator and
        // records where the trim started in the cropping rectangle, so what reaches the atlas is
        // the ink and what reaches the runtime is where to put it back. SAMPLE-062's
        // `NetRumbleFont.png` is the case that shows it -- an 8x27 cell whose only ink is one texel
        // answers bounds `[124, 1, 1, 1]` and a cropping of `[7, 26, 8, 27]`, and the sheet that
        // holds all 95 glyphs is 128x156 where the untrimmed cells needed 256x256
        // (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-131).
        //
        // A cell with no ink at all -- the space -- is the degenerate case, and XNA answers a 1x1
        // box at the bottom-right corner of the cell: the scan's initial minimum is the last texel
        // and the extent is clamped to one, which is exactly `[7, 26, 8, 27]` for an 8x27 cell.
        std::vector<RasterGlyph> glyphs;
        std::vector<Microsoft::Xna::Framework::Rectangle> croppings;
        std::vector<std::uint32_t> advances;
        glyphs.reserve(found.size());
        croppings.reserve(found.size());
        advances.reserve(found.size());
        for (const SheetGlyph& glyph : found)
        {
            std::uint32_t minX = glyph.width == 0u ? 0u : glyph.width - 1u;
            std::uint32_t minY = glyph.height == 0u ? 0u : glyph.height - 1u;
            std::uint32_t maxX = 0u;
            std::uint32_t maxY = 0u;
            for (std::uint32_t row = 0u; row < glyph.height; ++row)
            {
                for (std::uint32_t column = 0u; column < glyph.width; ++column)
                {
                    const std::size_t at =
                        ((static_cast<std::size_t>(glyph.y) + row) * width + glyph.x + column) * 4u;
                    // Ink is a texel that is neither the separator nor fully transparent. A real
                    // sheet has both: NetRumbleFont's cells are separated by magenta and *padded*
                    // with transparent white, and trimming on the separator alone finds no border
                    // to trim at all.
                    if (at + 3u >= rgba.size() || IsSeparator(&rgba[at]) || rgba[at + 3u] == 0u)
                    {
                        continue;
                    }
                    minX = std::min(minX, column);
                    minY = std::min(minY, row);
                    maxX = std::max(maxX, column);
                    maxY = std::max(maxY, row);
                }
            }
            const std::uint32_t inkWidth = maxX >= minX ? maxX - minX + 1u : 1u;
            const std::uint32_t inkHeight = maxY >= minY ? maxY - minY + 1u : 1u;

            RasterGlyph raster;
            raster.width = inkWidth;
            raster.height = inkHeight;
            raster.coverage.assign(static_cast<std::size_t>(inkWidth) * inkHeight * 4u, 0u);
            for (std::uint32_t row = 0u; row < inkHeight; ++row)
            {
                const std::size_t from =
                    ((static_cast<std::size_t>(glyph.y) + minY + row) * width + glyph.x + minX) * 4u;
                if (from + static_cast<std::size_t>(inkWidth) * 4u > rgba.size()) { break; }
                std::copy_n(rgba.begin() + static_cast<std::ptrdiff_t>(from),
                            static_cast<std::size_t>(inkWidth) * 4u,
                            raster.coverage.begin() +
                                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) *
                                                            inkWidth * 4u));
            }
            glyphs.push_back(std::move(raster));
            croppings.push_back(Microsoft::Xna::Framework::Rectangle(
                static_cast<SharpRuntime::intcs>(minX), static_cast<SharpRuntime::intcs>(minY),
                static_cast<SharpRuntime::intcs>(glyph.width),
                static_cast<SharpRuntime::intcs>(glyph.height)));
            advances.push_back(glyph.width);
        }

        std::vector<PackedGlyph> placements;
        std::uint32_t side = 0u;
        std::uint32_t usedHeight = 0u;
        if (!ChooseAtlas(glyphs, side, usedHeight, placements, 2u))
        {
            Fail(origin, "the glyphs in this sheet do not fit a " +
                             std::to_string(kMaximumAtlasSide) + "x" +
                             std::to_string(kMaximumAtlasSide) + " atlas");
        }

        Cnb::CnbSpriteFontData font;
        font.atlas.width = side;
        // The height is the rows used, rounded up to a multiple of four -- and then to a power of
        // two while it is still no more than 32.
        //
        // Seventeen genuine builds say so and none disagrees. Thirteen real fonts in the sample
        // corpus are *exactly* the used height rounded to a multiple of four and only one of them
        // is a power of two at all: `DebugFont` 64x112 from 111 rows, `NetRumbleFont` 128x156 from
        // 154, `LargeGameFont` 512x348 from 346. The four sheets in the differential corpus are
        // small enough for the second half to show: 16 rows answer 16, and 18 and 30 both answer
        // 32, where a multiple of four alone would answer 20 and 32
        // (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-131).
        //
        // The threshold is where the evidence runs out: "no more than 32 rows" and "narrower than
        // 64 texels" separate the same seventeen files, and nothing here distinguishes them.
        font.atlas.height = (usedHeight + 3u) & ~3u;
        if (font.atlas.height <= 32u) { font.atlas.height = RoundUpToPowerOfTwo(font.atlas.height); }
        font.atlas.depth = 1u;
        font.atlas.faceCount = 1u;
        font.atlas.mipCount = 1u;
        Cnb::CnbTextureRepresentation representation;
        representation.format = Cnb::CnbTextureFormat::Rgba8;
        representation.levels.emplace_back(
            static_cast<std::size_t>(side) * font.atlas.height * 4u, 0u);

        std::int32_t lineSpacing = 0;
        for (std::size_t index = 0u; index < glyphs.size(); ++index)
        {
            const RasterGlyph& glyph = glyphs[index];
            const PackedGlyph& placement = placements[index];
            for (std::uint32_t row = 0u; row < glyph.height; ++row)
            {
                const std::size_t to =
                    ((static_cast<std::size_t>(placement.y) + row) * side + placement.x) * 4u;
                std::copy_n(glyph.coverage.begin() +
                                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) *
                                                            glyph.width * 4u),
                            static_cast<std::size_t>(glyph.width) * 4u,
                            representation.levels.front().begin() +
                                static_cast<std::ptrdiff_t>(to));
            }
            font.glyphBounds.push_back(Microsoft::Xna::Framework::Rectangle(
                static_cast<SharpRuntime::intcs>(placement.x),
                static_cast<SharpRuntime::intcs>(placement.y),
                static_cast<SharpRuntime::intcs>(glyph.width),
                static_cast<SharpRuntime::intcs>(glyph.height)));
            // The cropping rectangle is where the trim started and how big the cell was; the
            // kerning triple is (0, *cell* width, 0), which is the advance a sheet can say
            // anything about -- the trimmed width is not it (measured, NetRumbleFont's kerning is
            // 8 for a glyph whose ink is one texel wide).
            font.cropping.push_back(croppings[index]);
            font.kerning.push_back(Microsoft::Xna::Framework::Vector3(
                0.0f, static_cast<float>(advances[index]), 0.0f));
            font.characters.push_back(static_cast<SharpRuntime::charcs>(
                static_cast<std::uint32_t>(firstCharacter) + static_cast<std::uint32_t>(index)));
            lineSpacing = std::max(lineSpacing, static_cast<std::int32_t>(glyph.height));
        }
        font.atlas.representations.push_back(std::move(representation));
        // Measured: the tallest glyph, a spacing of zero, and no default character.
        font.lineSpacing = lineSpacing;
        font.spacing = 0.0f;
        font.defaultCharacter.reset();
        return font;
    }

    ContentComponentIdentity FontTextureProcessor::Identity() const
    {
        return {kFontTextureProcessorName, "1"};
    }

    std::string FontTextureProcessor::InputType() const { return ImportedImageType; }

    std::string FontTextureProcessor::OutputType() const { return ProcessedSpriteFontType; }

    void FontTextureProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name == FontTextureFirstCharacterParameter ||
                name == TexturePremultiplyAlphaParameter || name == TextureFormatParameter)
            {
                continue;
            }
            throw ContentParameterError(
                ContentParameterFault::UnknownName, name,
                "FontTextureProcessor does not recognize parameter '" + name +
                    "'; it takes FirstCharacter, PremultiplyAlpha and TextureFormat.");
        }
    }

    ContentValue FontTextureProcessor::Process(const ContentValue& input,
                                               ContentProcessorContext& context) const
    {
        const ImportedImage& sheet = input.Get<ImportedImage>();
        SharpRuntime::charcs firstCharacter = u' ';
        if (const ContentProcessorParameterValue* named =
                context.Parameters().Find(FontTextureFirstCharacterParameter);
            named != nullptr)
        {
            // A `.contentproj` writes the character as text; a configuration document may write
            // its code point as a number. Both are what the property means.
            if (const std::string* text = std::get_if<std::string>(named); text != nullptr)
            {
                if (text->empty())
                {
                    throw ContentParameterError(ContentParameterFault::UnconvertibleValue,
                                                FontTextureFirstCharacterParameter,
                                                "FirstCharacter must be one character.");
                }
                firstCharacter =
                    static_cast<SharpRuntime::charcs>(static_cast<unsigned char>(text->front()));
            }
            else if (const std::int64_t* code = std::get_if<std::int64_t>(named); code != nullptr)
            {
                if (*code < 0 || *code > 0xFFFF)
                {
                    throw ContentParameterError(ContentParameterFault::UnconvertibleValue,
                                                FontTextureFirstCharacterParameter,
                                                "FirstCharacter must be a code point.");
                }
                firstCharacter = static_cast<SharpRuntime::charcs>(*code);
            }
            else
            {
                throw ContentParameterError(ContentParameterFault::UnconvertibleValue,
                                            FontTextureFirstCharacterParameter,
                                            "FirstCharacter must be one character or a code point.");
            }
        }

        Cnb::CnbSpriteFontData font = BuildFontFromTextureSheet(
            sheet.width, sheet.height, sheet.rgbaPixels, firstCharacter,
            CNA::Internal::ContentPathToUtf8(context.SourcePath()));

        // `PremultiplyAlpha` is this processor's own property and its default is True, which the
        // atlas shows: SAMPLE-062's `NetRumbleFont.png` pads its cells with *transparent white*
        // and XNA's atlas holds (0,0,0,0) there, not (255,255,255,0). CNA validated the parameter
        // and never applied it (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-131).
        bool premultiply = true;
        if (const ContentProcessorParameterValue* named =
                context.Parameters().Find(TexturePremultiplyAlphaParameter);
            named != nullptr)
        {
            if (const bool* value = std::get_if<bool>(named); value != nullptr)
            {
                premultiply = *value;
            }
            else if (const std::string* text = std::get_if<std::string>(named); text != nullptr)
            {
                std::string wanted = *text;
                std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                premultiply = wanted != "false" && wanted != "0";
            }
        }
        if (premultiply && !font.atlas.representations.empty() &&
            !font.atlas.representations.front().levels.empty())
        {
            std::vector<std::uint8_t>& texels = font.atlas.representations.front().levels.front();
            for (std::size_t at = 0u; at + 3u < texels.size(); at += 4u)
            {
                const std::uint32_t alpha = texels[at + 3u];
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    texels[at + channel] = static_cast<std::uint8_t>(
                        (static_cast<std::uint32_t>(texels[at + channel]) * alpha) / 255u);
                }
            }
        }
        context.LogInfo("found " + std::to_string(font.characters.size()) +
                        " glyph(s) in the sheet and packed them into a " +
                        std::to_string(font.atlas.width) + "x" +
                        std::to_string(font.atlas.height) + " atlas.");
        // Deliberately *not* compressed by default, where `FontDescriptionProcessor`'s atlas is.
        // The difference is XNA's own: this processor has a `TextureFormat` property whose default
        // is `Color`, and a genuine build of a sheet writes an uncompressed atlas
        // (`fonttexture/sheet_default`); the description processor has no such property and always
        // writes DXT3. A build that asks for `DxtCompressed` gets it.
        if (const ContentProcessorParameterValue* format =
                context.Parameters().Find(TextureFormatParameter);
            format != nullptr)
        {
            const std::string* named = std::get_if<std::string>(format);
            std::string wanted = named == nullptr ? std::string() : *named;
            std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (wanted == "dxtcompressed" || wanted == "dxt3")
            {
                CompressAtlasForXnb(font, context);
            }
            else if (wanted != "color" && wanted != "nochange" && !wanted.empty())
            {
                throw ContentParameterError(
                    ContentParameterFault::UnconvertibleValue, TextureFormatParameter,
                    "FontTextureProcessor takes NoChange, Color or DxtCompressed, not '" +
                        wanted + "'.");
            }
        }
        return ContentValue::Create(ProcessedSpriteFontType, std::move(font));
    }

    void RegisterSpriteFontSourceContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<const FontDescriptionImporter>());
        registry.RegisterProcessor(std::make_shared<const FontDescriptionProcessor>());
        registry.RegisterProcessor(std::make_shared<const FontTextureProcessor>());
    }
}
