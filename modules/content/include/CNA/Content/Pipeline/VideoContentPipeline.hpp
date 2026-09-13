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

    /**
     * @brief What a build-time probe answered about a streaming video source.
     *
     * XNA's own `VideoProcessor` gets these from the file, because the XNA build has a media stack
     * to ask. `cna_content` has none and must not grow one -- a game that loads a compiled video
     * needs no decoder -- so the probe is injected at registration by whoever does
     * (`cna_content_pipeline`, through `BuildTimeMedia::ProbeVideo`). Without one the processor
     * still works and simply requires the metadata as parameters, which is what it did before
     * (plans/plan_xnapipeline_parity.md `XNAPP-021`).
     */
    struct ProbedVideoMetadata
    {
        /** @brief Frame width in pixels. */
        std::uint32_t width = 0u;

        /** @brief Frame height in pixels. */
        std::uint32_t height = 0u;

        /** @brief Frames per second, as the stream's own rate. */
        float framesPerSecond = 0.0f;

        /** @brief Stream length in milliseconds. */
        std::uint32_t durationMs = 0u;

        /** @brief XNA's `VideoSoundtrackType`: 0 music, 1 dialog, 2 both. */
        std::uint32_t soundtrackType = 0u;

        /** @brief Compares the complete probed metadata. */
        bool operator==(const ProbedVideoMetadata&) const = default;
    };

    /**
     * @brief Reads a streaming video source's frame metadata, or answers nothing.
     *
     * @param source The native media file.
     * @return The metadata, or `std::nullopt` when this build cannot read the file.
     */
    using VideoMetadataProbe =
        std::function<std::optional<ProbedVideoMetadata>(const std::filesystem::path& source)>;

    /** @brief Source-oriented identity of a streaming video file without embedded media bytes. */
    struct ImportedVideoSource
    {
        /** @brief Canonical native source file copied as the streaming deployment artifact. */
        std::filesystem::path mediaSource;

        /** @brief Normalized generic UTF-8 media path relative to the source/content root. */
        std::string streamReference;

        /** @brief Source size used only for diagnostics; the primary dependency owns hashing. */
        std::uint64_t byteSize = 0u;

        /**
         * @brief What the registered probe read from the file, when there is one.
         *
         * Part of the imported value, and therefore part of what the incremental build compares:
         * a file whose frame rate changed is a different import even when its size did not.
         */
        std::optional<ProbedVideoMetadata> probed;

        /** @brief Compares the complete imported source identity. */
        bool operator==(const ImportedVideoSource&) const = default;
    };

    /** @brief HEADLESS importer for external media streamed by a runtime Video. */
    class VideoImporter final : public ContentImporter
    {
    public:
        /** @brief Creates an importer that records a source without reading its frame metadata. */
        VideoImporter() = default;

        /**
         * @brief Creates an importer that asks @p probe what the source's frames look like.
         *
         * @param probe The build-time probe; an empty one behaves as the default constructor.
         */
        explicit VideoImporter(VideoMetadataProbe probe);

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

    private:
        VideoMetadataProbe probe_;
    };

    /** @brief Produces canonical Video metadata and its separate runtime media XREF. */
    class VideoProcessor final : public ContentProcessor
    {
    public:
        /** @brief Creates a processor that requires the frame metadata as parameters. */
        VideoProcessor() = default;

        /**
         * @brief Creates a processor that may take the frame metadata from the imported source.
         *
         * @param metadataFromSource True when the registered importer probes the file, which is
         *        what makes `width`, `height` and `framesPerSecond` optional; a parameter still
         *        overrides what the probe read.
         */
        explicit VideoProcessor(bool metadataFromSource);

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

    private:
        bool metadataFromSource_ = false;
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
     * @param probe Optional build-time frame-metadata probe; without one a build must supply
     *        `width`, `height` and `framesPerSecond` as processor parameters.
     */
    void RegisterVideoContentPipeline(ContentPipelineRegistry& registry,
                                      VideoMetadataProbe probe = {});
}
