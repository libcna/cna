// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Import/ImportedSound.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Pipeline/ProcessedSoundEffect.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for source-oriented imported WAV audio. */
    inline constexpr const char* ImportedSoundType = "CNA.Content.Pipeline.ImportedSound";

    /**
     * @brief Stable in-memory type identity for processed, container-neutral SoundEffect data.
     *
     * It used to name `CNA.Content.Cnb.SoundEffectData`, and that was the defect rather than the
     * spelling: the CNB schema's 16-bit-only rule then decided what the XNA-compatible `.xnb`
     * writer was able to say, and an 8-bit source came out of both containers widened
     * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
     */
    inline constexpr const char* ProcessedSoundEffectType =
        "CNA.Content.Pipeline.ProcessedSoundEffect";

    /** @brief Headless WAV importer backed by CNA's existing bounded RIFF parser. */
    class WavImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the `.wav` source route. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the only imported type this component can produce.
         * @return A vector containing ImportedSoundType.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Parses a WAV into source-oriented PCM without opening an audio device.
         *
         * @param context Call-scoped importer context.
         * @return ImportedSound retaining the exact accepted source PCM encoding.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Converts imported WAV semantics into canonical runtime-oriented SoundEffect data. */
    class SoundEffectProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedSoundType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedSoundEffectType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Rejects every parameter because the initial exact PCM policy is fixed.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Settles the source PCM into container-neutral SoundEffect data.
         *
         * The source's own sample width is preserved, because that is what the genuine processor
         * does with every width it accepts (`XNASWEEP-197`).
         *
         * @param input ImportedSound value.
         * @param context Call-scoped processor context.
         * @return A ProcessedSoundEffect value.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative SoundEffect CNB codec. */
    class SoundEffectContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the frozen SoundEffect schema and encoder identity.
         * @return One stable SoundEffect asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /** @brief Returns ProcessedSoundEffectType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Adapts the processed sound to the CNB `SoundEffect` schema and encodes it.
         *
         * @param input ProcessedSoundEffect value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen SoundEffect asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Turns imported WAV semantics into the processor's own container-neutral answer.
     *
     * This is `SoundEffectProcessor`'s whole behaviour, exposed so that both writer adapters and
     * the tests can be written against one definition of what "processed" means. A source that
     * declares no loop is given the whole sound, which is what the genuine processor writes into
     * every `.xnb` measured here.
     *
     * @param imported The imported sound. Its encoding must be 8-bit unsigned or 16-bit PCM.
     * @return The processed sound, at the source's own width.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the channel count,
     *         sample rate, loop region or payload length is inconsistent, or the encoding is one
     *         the genuine importer refuses.
     */
    [[nodiscard]] ProcessedSoundEffect MakeProcessedSoundEffect(
        const CNA::Content::Import::ImportedSound& imported);

    /**
     * @brief Adapts a processed sound to the CNB `SoundEffect` schema's own description.
     *
     * @param sound The processed sound.
     * @return CNB `SoundEffect` data carrying the same width, rate, channels, frames and loop.
     */
    [[nodiscard]] Cnb::CnbSoundEffectData ToCnbSoundEffect(const ProcessedSoundEffect& sound);

    /**
     * @brief Decodes a compressed audio source to PCM, or answers nothing when it cannot.
     *
     * The decoder lives in the build-time module, which is the only place a media decoder is
     * linked; the canonical route takes it as a value so that a build without one behaves exactly
     * as it did before rather than growing a dependency (plans/plan_xnapipeline_parity.md
     * `XNAPP-332`, and `SongDurationProbe`'s own precedent).
     *
     * @param source The file to decode.
     * @return The decoded sound, or `std::nullopt` when this build cannot read the format.
     */
    using CompressedSoundDecoder = std::function<std::optional<CNA::Content::Import::ImportedSound>(
        const std::filesystem::path& source)>;

    /**
     * @brief Reads a compressed audio source as a sound effect, when a build asks for one.
     *
     * XNA has one `AudioContent`, so its `Mp3Importer` and `WmaImporter` feed `SongProcessor` and
     * `SoundEffectProcessor` alike; 14 of the sample corpus's `.wma` items ask for the second. CNA
     * has two imported types, so the second reading is this importer -- registered for the same
     * extensions and @ref SelectedByNameOnly, so a convention build of a `.mp3` still reaches the
     * song route and only a project that names a sound effect gets one.
     */
    class CompressedSoundImporter final : public ContentImporter
    {
    public:
        /**
         * @brief Creates an importer that decodes through @p decoder.
         *
         * @param decoder The build-time decoder; an empty one refuses every source by name.
         */
        explicit CompressedSoundImporter(CompressedSoundDecoder decoder);

        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the compressed source extensions XNA's own audio importers declare. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /** @brief Returns the one imported type this component produces. */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /** @brief Answers true: this reading of the extension is selected by name. */
        [[nodiscard]] bool SelectedByNameOnly() const override;

        /**
         * @brief Decodes the source to linear PCM.
         *
         * @param context Call-scoped importer context.
         * @return The decoded sound.
         * @throws std::runtime_error when this build has no decoder or cannot read the source.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;

    private:
        CompressedSoundDecoder decoder_;
    };

    /**
     * @brief Registers the built-in WAV importer, SoundEffect processor and writer.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterSoundEffectContentPipeline(ContentPipelineRegistry& registry);

    /**
     * @brief Registers the same, plus the compressed-source reading a project may ask for.
     *
     * @param registry Explicit registry to configure before builds begin.
     * @param decoder The build-time decoder the compressed importer reads through.
     */
    void RegisterSoundEffectContentPipeline(ContentPipelineRegistry& registry,
                                            CompressedSoundDecoder decoder);
}
