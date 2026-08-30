// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for an external streaming video source. */
    inline constexpr const char* ImportedVideoSourceType =
        "CNA.Content.Pipeline.ImportedVideoSource";

    /** @brief Stable in-memory type identity for processed Video CNB metadata. */
    inline constexpr const char* ProcessedVideoType = "CNA.Content.Cnb.VideoData";

    /** @brief VideoProcessor string parameter overriding the root-relative media XREF. */
    inline constexpr const char* VideoStreamReferenceParameter = "streamReference";

    /** @brief VideoProcessor optional u64 duration parameter, in milliseconds. */
    inline constexpr const char* VideoDurationMsParameter = "durationMs";

    /** @brief VideoProcessor required u64 frame-width parameter. */
    inline constexpr const char* VideoWidthParameter = "width";

    /** @brief VideoProcessor required u64 frame-height parameter. */
    inline constexpr const char* VideoHeightParameter = "height";

    /** @brief VideoProcessor required f64 frame-rate parameter. */
    inline constexpr const char* VideoFramesPerSecondParameter = "framesPerSecond";

    /** @brief VideoProcessor optional u64 VideoSoundtrackType value (0 through 2). */
    inline constexpr const char* VideoSoundtrackTypeParameter = "soundtrackType";

    /** @brief Source-oriented identity of a streaming video file without embedded media bytes. */
    struct ImportedVideoSource
    {
        /** @brief Canonical native source file copied as the streaming deployment artifact. */
        std::filesystem::path mediaSource;

        /** @brief Normalized generic UTF-8 media path relative to the source/content root. */
        std::string streamReference;

        /** @brief Source size used only for diagnostics; the primary dependency owns hashing. */
        std::uint64_t byteSize = 0u;

        /** @brief Compares the complete imported source identity. */
        bool operator==(const ImportedVideoSource&) const = default;
    };

    /** @brief HEADLESS importer for external media streamed by a runtime Video. */
    class VideoImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns unambiguous streaming-video extensions supported by CNA runtime. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the only imported type this component can produce.
         * @return A vector containing ImportedVideoSourceType.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Records a contained external-media source without decoding or embedding it.
         *
         * @param context Call-scoped importer context.
         * @return Root-relative stream identity and checked source size.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Produces canonical Video metadata and its separate runtime media XREF. */
    class VideoProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedVideoSourceType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedVideoType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Validates required frame metadata and optional stream metadata parameters.
         *
         * @param parameters Parameters to validate before metadata construction.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Constructs canonical CnbVideoData without reading media payload bytes.
         *
         * @param input ImportedVideoSource value.
         * @param context Processor context containing explicit frame metadata.
         * @return Canonical CnbVideoData boxed as ProcessedVideoType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative Video CNB codec. */
    class VideoContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the frozen Video schema and encoder identity.
         * @return One stable Video asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /** @brief Returns ProcessedVideoType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing EncodeVideoToCnb() implementation.
         *
         * @param input Canonical CnbVideoData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen Video asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Registers the built-in Video importer, processor, and writer adapter.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterVideoContentPipeline(ContentPipelineRegistry& registry);
}
