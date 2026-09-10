// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"

#include <stdexcept>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Import/ImportedSound.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kWavImporterName = "CNA.WavImporter";
        constexpr const char* kSoundEffectProcessorName = "CNA.SoundEffectProcessor";
        constexpr const char* kSoundEffectWriterName = "CNA.SoundEffectContentWriter";
        constexpr const char* kCompressedSoundImporterName = "CNA.CompressedSoundImporter";
    }

    ContentComponentIdentity WavImporter::Identity() const
    {
        return {kWavImporterName, "2"};
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
        // The genuine importer refuses every width past 16 bits, and says so in those words:
        // *"Audio file X contains 24-bit audio. Only 8-bit and 16-bit audio data is supported."*
        // and *"contains non-PCM data."* for IEEE float. Measured over the whole matrix
        // (`audio-content-oracle.json`, `processors/SoundEffectProcessor_pcm_matrix`). Refusing
        // here rather than narrowing later is what keeps the processor's answer lossless; the
        // `.cnb`-only route through `ImportWavAsCnbSoundEffect` still converts, because it is not
        // claiming to be XNA (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
        using CNA::Content::Import::ImportedPcmEncoding;
        if (imported.encoding != ImportedPcmEncoding::Unsigned8 &&
            imported.encoding != ImportedPcmEncoding::Signed16LittleEndian)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "Audio file " + CNA::Internal::ContentPathToUtf8(context.SourcePath()) + " contains " +
                CNA::Content::Import::ImportedPcmEncodingName(imported.encoding) +
                ". Only 8-bit and 16-bit audio data is supported.");
        }
        context.LogInfo(
            std::string("decoded ") +
            CNA::Content::Import::ImportedPcmEncodingName(imported.encoding) + " with " +
            std::to_string(imported.frameCount) + " frames, " +
            std::to_string(imported.channels) + " channel(s), and " +
            std::to_string(imported.sampleRate) + " Hz sample rate.");
        return ContentValue::Create(ImportedSoundType, std::move(imported));
    }

    ContentComponentIdentity SoundEffectProcessor::Identity() const
    {
        // Build version 3: the processed value is container-neutral and keeps the source's own
        // sample width, so an 8-bit source built by version 2 -- which widened it to 16 -- is a
        // different asset (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
        return {kSoundEffectProcessorName, "3"};
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

    ProcessedSoundEffect MakeProcessedSoundEffect(
        const CNA::Content::Import::ImportedSound& imported)
    {
        using CNA::Content::Import::ImportedPcmEncoding;
        ProcessedSoundEffect sound;
        switch (imported.encoding)
        {
            case ImportedPcmEncoding::Unsigned8:
                sound.encoding = ProcessedPcmEncoding::Unsigned8;
                break;
            case ImportedPcmEncoding::Signed16LittleEndian:
                sound.encoding = ProcessedPcmEncoding::Signed16LittleEndian;
                break;
            default:
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    std::string("SoundEffect processing: the source is ") +
                    CNA::Content::Import::ImportedPcmEncodingName(imported.encoding) +
                    ". Only 8-bit and 16-bit audio data is supported.");
        }
        if (imported.channels != 1u && imported.channels != 2u)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "SoundEffect processing: imported PCM must be mono or stereo.");
        }
        if (imported.sampleRate == 0u || imported.sampleRate > Cnb::CnbMaxAudioSampleRate)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "SoundEffect processing: imported PCM sample rate is outside 1.." +
                std::to_string(Cnb::CnbMaxAudioSampleRate) + " Hz.");
        }
        if (static_cast<std::uint64_t>(imported.loopStart) + imported.loopLength >
            imported.frameCount)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "SoundEffect processing: imported loop region exceeds the frame count.");
        }
        sound.sampleRate = imported.sampleRate;
        sound.channels = imported.channels;
        sound.frameCount = imported.frameCount;
        sound.loopStart = imported.loopStart;
        // A source that declares no loop is given the whole sound, which is what every genuine
        // `.xnb` measured here carries: `pcm8_mono_22050.wav` has no `smpl` chunk and its
        // reference records loopStart 0 and loopLength 441, the frame count
        // (`tests/reference/xna40/differential-sound-widths/`).
        sound.loopLength = (imported.loopStart == 0u && imported.loopLength == 0u)
                               ? imported.frameCount
                               : imported.loopLength;
        const std::uint64_t expected = static_cast<std::uint64_t>(imported.frameCount) *
                                       imported.channels *
                                       ProcessedPcmSampleBytes(sound.encoding);
        if (imported.samples.size() != expected)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "SoundEffect processing: imported PCM holds " +
                std::to_string(imported.samples.size()) + " bytes, but " +
                std::to_string(imported.frameCount) + " frame(s) x " +
                std::to_string(imported.channels) + " channel(s) need " +
                std::to_string(expected) + ".");
        }
        sound.samples = imported.samples;
        return sound;
    }

    Cnb::CnbSoundEffectData ToCnbSoundEffect(const ProcessedSoundEffect& sound)
    {
        Cnb::CnbSoundEffectData data;
        data.format = sound.encoding == ProcessedPcmEncoding::Unsigned8
                          ? Cnb::CnbAudioFormat::Pcm8
                          : Cnb::CnbAudioFormat::Pcm16;
        data.sampleRate = sound.sampleRate;
        data.channels = sound.channels;
        data.frameCount = sound.frameCount;
        data.loopStart = sound.loopStart;
        data.loopLength = sound.loopLength;
        data.samples = sound.samples;
        return data;
    }

    ContentValue SoundEffectProcessor::Process(const ContentValue& input,
                                               ContentProcessorContext& context) const
    {
        const CNA::Content::Import::ImportedSound& imported =
            input.Get<CNA::Content::Import::ImportedSound>();
        ProcessedSoundEffect sound = MakeProcessedSoundEffect(imported);
        context.LogInfo(std::string("prepared SoundEffect ") +
                        ProcessedPcmEncodingName(sound.encoding) + " data with " +
                        std::to_string(sound.frameCount) + " frame(s) at " +
                        std::to_string(sound.sampleRate) + " Hz.");
        return ContentValue::Create(ProcessedSoundEffectType, std::move(sound));
    }

    ContentComponentIdentity SoundEffectContentWriter::Identity() const
    {
        // Build version 2: the schema this writer emits is 2 rather than 1, so its bytes differ
        // for unchanged input (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
        return {kSoundEffectWriterName, "2"};
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
        const Cnb::CnbSoundEffectData sound =
            ToCnbSoundEffect(input.Get<ProcessedSoundEffect>());
        return {Cnb::EncodeSoundEffectToCnb(sound, logicalName),
                Cnb::CnbAssetTypeId::SoundEffect,
                "Microsoft.Xna.Framework.Audio.SoundEffect",
                Cnb::CnbSoundEffectSchemaVersion};
    }

    CompressedSoundImporter::CompressedSoundImporter(CompressedSoundDecoder decoder)
        : decoder_(std::move(decoder))
    {
    }

    ContentComponentIdentity CompressedSoundImporter::Identity() const
    {
        return {kCompressedSoundImporterName, "1"};
    }

    std::vector<std::string> CompressedSoundImporter::SourceExtensions() const
    {
        // Exactly the two XNA's own audio importers declare. The song route reads more formats
        // than these, but a project can only ask for this reading with a name XNA has, and XNA has
        // only `Mp3Importer` and `WmaImporter`.
        return {".mp3", ".wma"};
    }

    std::vector<std::string> CompressedSoundImporter::OutputTypes() const
    {
        return {ImportedSoundType};
    }

    bool CompressedSoundImporter::SelectedByNameOnly() const { return true; }

    ContentValue CompressedSoundImporter::Import(ContentImporterContext& context) const
    {
        if (!decoder_)
        {
            throw std::runtime_error(
                "this build has no audio decoder, so a compressed source cannot be read as a "
                "sound effect; build it as a song, or use a build with a decoder.");
        }
        std::optional<CNA::Content::Import::ImportedSound> decoded =
            decoder_(context.SourcePath());
        if (!decoded.has_value())
        {
            throw std::runtime_error("the audio decoder could not read this source.");
        }
        if (decoded->channels == 0u || decoded->sampleRate == 0u || decoded->frameCount == 0u)
        {
            throw std::runtime_error("the decoded audio has no frames.");
        }
        context.LogInfo("decoded " +
                        std::string(CNA::Content::Import::ImportedPcmEncodingName(
                            decoded->encoding)) +
                        " with " + std::to_string(decoded->frameCount) + " frames, " +
                        std::to_string(decoded->channels) + " channel(s), and " +
                        std::to_string(decoded->sampleRate) + " Hz sample rate.");
        return ContentValue::Create(ImportedSoundType, std::move(*decoded));
    }

    void RegisterSoundEffectContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<WavImporter>());
        registry.RegisterProcessor(std::make_shared<SoundEffectProcessor>());
        registry.RegisterWriter(std::make_shared<SoundEffectContentWriter>());
    }

    void RegisterSoundEffectContentPipeline(ContentPipelineRegistry& registry,
                                            CompressedSoundDecoder decoder)
    {
        RegisterSoundEffectContentPipeline(registry);
        registry.RegisterImporter(
            std::make_shared<CompressedSoundImporter>(std::move(decoder)));
    }
}
