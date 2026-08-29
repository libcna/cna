// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"

#include <stdexcept>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Import/ImportedSound.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kWavImporterName = "CNA.WavImporter";
        constexpr const char* kSoundEffectProcessorName = "CNA.SoundEffectProcessor";
        constexpr const char* kSoundEffectWriterName = "CNA.SoundEffectContentWriter";
    }

    ContentComponentIdentity WavImporter::Identity() const
    {
        return {kWavImporterName, "1"};
    }

    std::vector<std::string> WavImporter::SourceExtensions() const
    {
        return {".wav"};
    }

    std::vector<std::string> WavImporter::OutputTypes() const
    {
        return {ImportedSoundType};
    }

    ContentValue WavImporter::Import(ContentImporterContext& context) const
    {
        CNA::Content::Import::ImportedSound imported =
            Cnb::ImportWavAsImportedSound(context.SourcePath());
        context.LogInfo("decoded WAV with " + std::to_string(imported.frameCount) +
                        " frames, " + std::to_string(imported.channels) + " channel(s), and " +
                        std::to_string(imported.sampleRate) + " Hz sample rate.");
        return ContentValue::Create(ImportedSoundType, std::move(imported));
    }

    ContentComponentIdentity SoundEffectProcessor::Identity() const
    {
        return {kSoundEffectProcessorName, "1"};
    }

    std::string SoundEffectProcessor::InputType() const
    {
        return ImportedSoundType;
    }

    std::string SoundEffectProcessor::OutputType() const
    {
        return ProcessedSoundEffectType;
    }

    void SoundEffectProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        if (!parameters.Empty())
        {
            throw std::invalid_argument(
                "SoundEffectProcessor does not accept processor parameters.");
        }
    }

    ContentValue SoundEffectProcessor::Process(const ContentValue& input,
                                               ContentProcessorContext& context) const
    {
        const CNA::Content::Import::ImportedSound& imported =
            input.Get<CNA::Content::Import::ImportedSound>();
        Cnb::CnbSoundEffectData sound = Cnb::ProcessImportedSoundEffect(imported);
        context.LogInfo("prepared SoundEffect Pcm16 data for CNB encoding.");
        return ContentValue::Create(ProcessedSoundEffectType, std::move(sound));
    }

    ContentComponentIdentity SoundEffectContentWriter::Identity() const
    {
        return {kSoundEffectWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    SoundEffectContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::SoundEffect, Cnb::CnbSoundEffectSchemaVersion,
                 "Microsoft.Xna.Framework.Audio.SoundEffect",
                 {"CNA.Cnb.EncodeSoundEffectToCnb", "1"}}};
    }

    std::string SoundEffectContentWriter::InputType() const
    {
        return ProcessedSoundEffectType;
    }

    ContentWriteResult SoundEffectContentWriter::Write(const ContentValue& input,
                                                       const std::string& logicalName) const
    {
        const Cnb::CnbSoundEffectData& sound = input.Get<Cnb::CnbSoundEffectData>();
        return {Cnb::EncodeSoundEffectToCnb(sound, logicalName),
                Cnb::CnbAssetTypeId::SoundEffect,
                "Microsoft.Xna.Framework.Audio.SoundEffect"};
    }

    void RegisterSoundEffectContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<WavImporter>());
        registry.RegisterProcessor(std::make_shared<SoundEffectProcessor>());
        registry.RegisterWriter(std::make_shared<SoundEffectContentWriter>());
    }
}
