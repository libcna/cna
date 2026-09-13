// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Stable in-memory type identity for a parsed `.spritefont` description.
     *
     * Distinct from ImportedSpriteFontType, which is the already-rasterized font a CNJ sidecar
     * carries. This one has not been rasterized yet: it names a typeface and a character set.
     */
    inline constexpr const char* ImportedFontDescriptionType =
        "CNA.Content.Pipeline.ImportedFontDescription";

    /** @brief Typeface style requested by a font description. */
    enum class FontDescriptionStyle
    {
        /** @brief No emphasis. */
        Regular,
        /** @brief Bold. */
        Bold,
        /** @brief Italic. */
        Italic,
        /** @brief Bold and italic together. */
        BoldItalic,
    };

    /** @brief One inclusive range of characters a font must render. */
    struct FontCharacterRegion
    {
        /** @brief First character in the range. */
        SharpRuntime::charcs start = u'\0';

        /** @brief Last character in the range, inclusive. */
        SharpRuntime::charcs end = u'\0';

        /** @brief Compares both endpoints. */
        bool operator==(const FontCharacterRegion&) const = default;
    };

    /**
     * @brief A parsed `.spritefont`: what to rasterize, not the result of rasterizing it
     *        (plans/plan_xnapipeline.md `XNAP-50`).
     *
     * The field names are XNA's own, because the file format is XNA's own.
     */
    struct FontDescription
    {
        /** @brief Typeface name, or a font file path relative to the description. */
        std::string fontName;

        /** @brief Em size in points, rasterized at 96 dots per inch. */
        float size = 12.0f;

        /** @brief Extra horizontal space inserted between glyphs at draw time. */
        float spacing = 0.0f;

        /** @brief Whether per-glyph side bearings are kept, or folded into the advance. */
        bool useKerning = true;

        /** @brief Requested typeface style. */
        FontDescriptionStyle style = FontDescriptionStyle::Regular;

        /** @brief Fallback character for text the font does not cover, or absent to throw. */
        std::optional<SharpRuntime::charcs> defaultCharacter;

        /** @brief Character ranges to rasterize, in authored order. */
        std::vector<FontCharacterRegion> characterRegions;

        /** @brief Resolved font file, decided by the importer rather than at rasterization. */
        std::filesystem::path resolvedFontFile;

        /** @brief Whether @ref resolvedFontFile is a system font rather than a project file. */
        bool resolvedFromSystemFonts = false;

        /** @brief Compares every authored and resolved field. */
        bool operator==(const FontDescription&) const = default;
    };

    /**
     * @brief Parses the `.spritefont` XML XNA Game Studio authored.
     *
     * @param xml Complete UTF-8 document.
     * @param origin Path used in diagnostics.
     * @return The description, with @ref FontDescription::resolvedFontFile still empty.
     * @throws std::runtime_error naming the offending element for any malformed document.
     */
    [[nodiscard]] FontDescription ParseFontDescription(const std::string& xml,
                                                       const std::string& origin);

    /**
     * @brief Returns every character a description asks for, ascending and deduplicated.
     *
     * @param description The parsed description.
     * @return The character set in the order a SpriteFont stores it.
     * @throws std::runtime_error for an inverted or oversized region.
     */
    [[nodiscard]] std::vector<SharpRuntime::charcs> ExpandCharacterRegions(
        const FontDescription& description);

    /** @brief Whether this build can rasterize fonts at all. */
    [[nodiscard]] bool IsFontRasterizationAvailable() noexcept;

    /**
     * @brief Rasterizes a description into the canonical SpriteFont representation.
     *
     * The atlas layout is CNA's own: a deterministic shelf packing into the smallest power-of-two
     * square that fits, with one texel of padding between glyphs. XNA's own packing is not
     * publicly specified and does not need to be reproduced -- what must be reproduced is the
     * *semantics* of the four parallel arrays, which is what SpriteBatch::DrawString() and
     * SpriteFont::MeasureString() consume.
     *
     * @param description Parsed description with its font file already resolved.
     * @param warnings Receives one entry per documented approximation, such as a synthesized
     *        bold or italic face.
     * @return Canonical CNB SpriteFont data, ready for either output container.
     * @throws std::runtime_error when the font cannot be opened, a glyph is missing, the atlas
     *         would exceed the maximum texture size, or this build has no rasterizer.
     */
    /**
     * @brief Searches the font directories for a family, then for a file of that name.
     *
     * `<FontName>` is a font *family* name -- XNA resolves it through Windows -- and the family is
     * looked for first, by reading each candidate's own family table and choosing the face whose
     * style answers @p style. The file-name match remains as the fallback, and is all a build with
     * no rasterizer can do.
     *
     * @param fontName The font name as authored.
     * @param style The style the description asked for.
     * @return The chosen path in a deterministic walk, or an empty path.
     */
    [[nodiscard]] std::filesystem::path FindSystemFontFile(
        const std::string& fontName, FontDescriptionStyle style = FontDescriptionStyle::Regular);

    /**
     * @brief Looks for a font family among the font files directly inside one directory.
     *
     * @param directory The directory to look in; not searched recursively.
     * @param fontName The family name as authored.
     * @param style The style the description asked for.
     * @return The chosen path, or an empty path.
     */
    [[nodiscard]] std::filesystem::path FindFontFamilyBeside(
        const std::filesystem::path& directory, const std::string& fontName,
        FontDescriptionStyle style = FontDescriptionStyle::Regular);

    /**
     * @brief Sets the directories a build adds to the font search, ahead of the platform's own.
     *
     * A game whose fonts ship with it rather than being installed -- which is every game built on
     * a machine that is not the artist's -- needs a way to say where they are. `CNA_FONT_PATH` in
     * the environment says the same thing, and is read after these.
     *
     * @param directories The directories, in the order they are to be searched.
     */
    void SetFontSearchDirectoriesEXT(std::vector<std::filesystem::path> directories);

    /** @brief The directories a build added to the font search. */
    [[nodiscard]] const std::vector<std::filesystem::path>& FontSearchDirectoriesEXT() noexcept;

    /**
     * @brief Rasterizes a resolved font description into a sprite-font atlas.
     *
     * @param description The description, with its font file already resolved.
     * @param warnings Receives every warning the rasterization produced.
     * @param strictness Whether a character the font has no glyph for refuses the build or is
     *        drawn with the font's own `.notdef` and warned about, which is what XNA does
     *        (plans/plan_xnapipeline_parity.md XNAPP-267).
     * @param profile The graphics profile the atlas must suit: Reach rounds its height up to a
     *        power of two, HiDef up to four (measured, plans/plan_xna_sample_xnb_sweep.md
     *        XNASWEEP-104).
     * @return The atlas and its glyph table.
     */
    [[nodiscard]] Cnb::CnbSpriteFontData RasterizeFontDescription(
        const FontDescription& description, std::vector<std::string>& warnings,
        ContentStrictness strictness = ContentStrictness::Strict,
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile =
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach);

    /** @brief Reads a `.spritefont` and resolves the font file it names. */
    class FontDescriptionImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the `.spritefont` source route. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the only imported type this component can produce.
         * @return A vector containing ImportedFontDescriptionType.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Parses the description and records the font file as a build dependency.
         *
         * @param context Call-scoped importer context.
         * @return A FontDescription whose font file is resolved and recorded.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Rasterizes a font description into canonical SpriteFont data. */
    class FontDescriptionProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedFontDescriptionType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedSpriteFontType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Rejects every parameter; the description already carries the whole policy.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Rasterizes the glyphs and packs them into one atlas.
         *
         * @param input FontDescription value.
         * @param context Call-scoped processor context, used to report approximations.
         * @return Canonical CnbSpriteFontData boxed as ProcessedSpriteFontType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /**
     * @brief The character the first glyph of a font sheet is; `FontTextureProcessor`'s own
     *        `FirstCharacter`, whose XNA default is the space.
     */
    inline constexpr const char* FontTextureFirstCharacterParameter = "firstCharacter";

    /**
     * @brief Builds a sprite font out of a sheet of glyph images.
     *
     * XNA's `FontTextureProcessor` reads a texture whose glyphs are separated by **magenta**
     * (255, 0, 255) -- measured, and it is that colour and not the sheet's own top-left texel: a
     * sheet bordered in transparent black is refused with the same sentence an empty one is. Each
     * non-magenta rectangle is one glyph, taken in reading order, and the characters run
     * consecutively from `firstCharacter`. Everything else follows from the glyphs: the line
     * spacing is the tallest of them, the spacing is zero, each cropping rectangle is the glyph's
     * own size at the origin, and each kerning triple is `(0, width, 0)` -- all four measured from
     * genuine builds of three sheets (`tools/xna-pipeline-oracle/differential/fonttexture.json`,
     * plans/plan_xnapipeline_parity.md `XNAPP-139`).
     *
     * @param width Sheet width in texels.
     * @param height Sheet height in texels.
     * @param rgba Sheet pixels, R, G, B, A per texel.
     * @param firstCharacter The character the first glyph is.
     * @param origin Path named in a refusal.
     * @param profile The graphics profile the atlas must suit: Reach rounds its height up to a
     *        power of two, HiDef to a multiple of four.
     * @return The packed font.
     * @throws InvalidContentException when the sheet holds no glyph, in XNA's own words.
     */
    [[nodiscard]] Cnb::CnbSpriteFontData BuildFontFromTextureSheet(
        std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba,
        SharpRuntime::charcs firstCharacter, const std::string& origin,
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile =
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach);

    /**
     * @brief Turns a sheet of glyph images into canonical SpriteFont data.
     *
     * Registered against the same imported type the texture route takes, and
     * @ref ContentProcessor::SelectedByNameOnly so it never competes with it: a `.png` builds as a
     * texture unless a project names this processor, which is exactly what XNA does.
     */
    class FontTextureProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedImageType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedSpriteFontType. */
        [[nodiscard]] std::string OutputType() const override;

        /** @brief Accepts `firstCharacter`, `premultiplyAlpha` and `textureFormat`. */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /** @brief Never chosen for an imported image unless a build names it. */
        [[nodiscard]] bool SelectedByNameOnly() const override { return true; }

        /**
         * @brief Finds the glyphs, packs them and answers the font.
         *
         * @param input ImportedImage value.
         * @param context Call-scoped processor context.
         * @return Canonical CnbSpriteFontData boxed as ProcessedSpriteFontType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /**
     * @brief Registers the `.spritefont` importer and its rasterizing processor.
     *
     * The existing SpriteFont writers -- CNB's and XNB's -- consume the processed value unchanged,
     * so registering this adds a source route to both containers at once.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterSpriteFontSourceContentPipeline(ContentPipelineRegistry& registry);
}
