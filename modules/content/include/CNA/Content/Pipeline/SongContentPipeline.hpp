// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for an external streaming song source. */
    inline constexpr const char* ImportedSongSourceType =
        "CNA.Content.Pipeline.ImportedSongSource";

    /** @brief Stable in-memory type identity for processed Song CNB metadata. */
    inline constexpr const char* ProcessedSongType = "CNA.Content.Cnb.SongData";

    /** @brief SongProcessor string parameter overriding the root-relative media XREF. */
    inline constexpr const char* SongStreamReferenceParameter = "streamReference";

    /** @brief SongProcessor string parameter containing the optional display name. */
    inline constexpr const char* SongNameParameter = "name";

    /** @brief SongProcessor u64 parameter containing duration in milliseconds, or zero. */
    inline constexpr const char* SongDurationMsParameter = "durationMs";

    /**
     * @brief Reads a streaming song's duration, or answers nothing.
     *
     * The same arrangement the video route has, and for the same reason: `cna_content` has no
     * decoder and must not grow one, so the importer takes a probe from whoever registers it and
     * the build-time layer supplies one over the media decoder. Without it a `Song` carries
     * duration zero -- which a genuine XNA 4.0 runtime reports as `Song.Duration` of zero, where
     * XNA's own build reports the real length (plans/plan_xnapipeline_parity.md XNAPP-281).
     *
     * @param source The native media file.
     * @return The duration in milliseconds, or `std::nullopt` when this build cannot read it.
     */
    using SongDurationProbe =
        std::function<std::optional<std::uint32_t>(const std::filesystem::path& source)>;

    /** @brief Source-oriented identity of a streaming song file without embedded media bytes. */
    struct ImportedSongSource
    {
        /** @brief Canonical native source file copied as the streaming deployment artifact. */
        std::filesystem::path mediaSource;

        /** @brief Normalized generic UTF-8 media path relative to the source/content root. */
        std::string streamReference;

        /** @brief Source size used only for diagnostics; the primary dependency owns hashing. */
        std::uint64_t byteSize = 0u;

        /** @brief Optional display name authored by a compatibility container. */
        std::optional<std::string> authoredName;

        /** @brief Optional duration authored by a compatibility container, in milliseconds. */
        std::optional<std::uint32_t> authoredDurationMs;

        /** @brief Compares the complete imported source identity. */
        bool operator==(const ImportedSongSource&) const = default;
    };

    /** @brief HEADLESS importer for external audio streamed by a runtime Song. */
    class SongImporter final : public ContentImporter
    {
    public:
        /** @brief Creates an importer that reads no duration. */
        SongImporter() = default;

        /**
         * @brief Creates an importer that asks @p probe how long the source is.
         *
         * @param probe The build-time probe; an empty one behaves as the default constructor.
         */
        explicit SongImporter(SongDurationProbe probe);

        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns unambiguous streaming-song extensions supported by CNA runtime. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the only imported type this component can produce.
         * @return A vector containing ImportedSongSourceType.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Records a contained external-media source without decoding or embedding it.
         *
         * @param context Call-scoped importer context.
         * @return Root-relative stream identity and checked source size.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;

    private:
        SongDurationProbe probe_;
    };

    /** @brief Produces canonical Song metadata and its separate runtime media XREF. */
    class SongProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedSongSourceType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedSongType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Validates streamReference, name, and durationMs parameters.
         *
         * @param parameters Parameters to validate before metadata construction.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Constructs canonical CnbSongData without reading media payload bytes.
         *
         * @param input ImportedSongSource value.
         * @param context Processor context containing metadata parameters.
         * @return Canonical CnbSongData boxed as ProcessedSongType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative Song CNB codec. */
    class SongContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the frozen Song schema and encoder identity.
         * @return One stable Song asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /** @brief Returns ProcessedSongType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing EncodeSongToCnb() implementation.
         *
         * @param input Canonical CnbSongData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen Song asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Registers the built-in Song importer, processor, and writer adapter.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterSongContentPipeline(ContentPipelineRegistry& registry,
                                     SongDurationProbe probe = {});
}
