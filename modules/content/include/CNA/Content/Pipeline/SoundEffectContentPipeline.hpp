// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for source-oriented imported WAV audio. */
    inline constexpr const char* ImportedSoundType = "CNA.Content.Pipeline.ImportedSound";

    /** @brief Stable in-memory type identity for processed SoundEffect CNB data. */
    inline constexpr const char* ProcessedSoundEffectType = "CNA.Content.Cnb.SoundEffectData";

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
         * @brief Converts accepted source PCM into CNB's Pcm16 representation.
         *
         * @param input ImportedSound value.
         * @param context Call-scoped processor context.
         * @return Canonical CnbSoundEffectData value.
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

        /** @brief Returns ProcessedSoundEffectType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing EncodeSoundEffectToCnb() implementation.
         *
         * @param input Canonical CnbSoundEffectData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen SoundEffect asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Registers the built-in WAV importer, SoundEffect processor and writer.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterSoundEffectContentPipeline(ContentPipelineRegistry& registry);
}
