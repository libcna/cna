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
     * @brief Searches the system font directories for a file whose stem matches a name.
     *
     * Deliberately a filename match rather than a family-name lookup, for the reason the
     * importer's own use of it states: reading a font's internal family table would need the
     * rasterizer, which an unconfigured build does not have.
     *
     * @param fontName The font name as authored.
     * @return The first matching path in a deterministic walk, or an empty path.
     */
    [[nodiscard]] std::filesystem::path FindSystemFontFile(const std::string& fontName);

    [[nodiscard]] Cnb::CnbSpriteFontData RasterizeFontDescription(
        const FontDescription& description, std::vector<std::string>& warnings);

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
     * @brief Registers the `.spritefont` importer and its rasterizing processor.
     *
     * The existing SpriteFont writers -- CNB's and XNB's -- consume the processed value unchanged,
     * so registering this adds a source route to both containers at once.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterSpriteFontSourceContentPipeline(ContentPipelineRegistry& registry);
}
