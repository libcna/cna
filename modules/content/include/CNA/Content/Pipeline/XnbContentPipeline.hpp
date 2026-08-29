// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable imported type for Video metadata carried by a supported XNB root. */
    inline constexpr const char* ImportedXnbVideoType =
        "CNA.Content.Pipeline.ImportedXnbVideo";

    /** @brief Source-oriented XNB Video metadata plus its validated external-media identity. */
    struct ImportedXnbVideo
    {
        /** @brief Canonical native source file copied as the streaming deployment artifact. */
        std::filesystem::path mediaSource;

        /** @brief Canonical native Video metadata decoded from the XNB payload. */
        Cnb::CnbVideoData data;
    };

    /**
     * @brief Headless compatibility importer for explicitly supported built-in XNB roots.
     *
     * Container/header/compression/type-table validation and wire decoding are shared with the
     * runtime XNB readers. The importer returns existing source-oriented pipeline values and
     * never carries original XNB bytes into CNB output.
     */
    class XnbImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the `.xnb` compatibility source route. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns every bounded source-oriented type a supported XNB root may produce.
         * @return Stable imported type identities in deterministic order.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Validates and decodes one supported built-in XNB root headlessly.
         *
         * @param context Call-scoped importer context.
         * @return Existing canonical pipeline value selected from the validated root reader.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Preserves XNB-authored Video metadata while allowing explicit config overrides. */
    class XnbVideoProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable XNB Video processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedXnbVideoType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedVideoType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Validates optional standard Video metadata overrides.
         *
         * @param parameters Processor parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Produces canonical Video data and records its runtime XREF.
         *
         * @param input ImportedXnbVideo value.
         * @param context Processor context containing optional overrides.
         * @return CnbVideoData boxed as ProcessedVideoType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /**
     * @brief Registers XNB source import and the XNB-specific Video metadata adapter.
     *
     * Existing Texture, SpriteFont, SoundEffect, Curve, Song, and Video writers/processors remain
     * owned by their established pipeline slices and must be registered alongside this function.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterXnbContentPipeline(ContentPipelineRegistry& registry);
}
