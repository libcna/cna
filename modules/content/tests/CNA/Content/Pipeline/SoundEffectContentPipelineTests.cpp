// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Import/ImportedSound.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
namespace Import = CNA::Content::Import;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_sound_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::vector<std::uint8_t> MakePcm16(std::size_t frames, std::uint16_t channels)
    {
        std::vector<std::uint8_t> samples(frames * channels * 2u);
        for (std::size_t index = 0u; index < frames * channels; ++index)
        {
            const auto signedValue =
                static_cast<std::int16_t>((index * 73u) % 30000u - 15000);
            const auto value = static_cast<std::uint16_t>(signedValue);
            samples[index * 2u] = static_cast<std::uint8_t>(value & 0xFFu);
            samples[index * 2u + 1u] = static_cast<std::uint8_t>(value >> 8u);
        }
        return samples;
    }

    std::vector<std::uint8_t> MakeWav(std::uint16_t channels, std::uint32_t sampleRate,
                                      std::uint16_t bitsPerSample,
                                      const std::vector<std::uint8_t>& samples,
                                      bool includeLoop = false,
                                      std::uint32_t loopStart = 0u,
                                      std::uint32_t loopEnd = 0u)
    {
        std::vector<std::uint8_t> output;
        const auto writeU16 = [&](std::uint16_t value)
        {
            output.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            output.push_back(static_cast<std::uint8_t>(value >> 8u));
        };
        const auto writeU32 = [&](std::uint32_t value)
        {
            for (int byte = 0; byte < 4; ++byte)
            {
                output.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
            }
        };
        const auto writeTag = [&](const char* text)
        {
            for (int index = 0; index < 4; ++index)
            {
                output.push_back(static_cast<std::uint8_t>(text[index]));
            }
        };

        std::vector<std::uint8_t> loop;
        if (includeLoop)
        {
            loop.resize(60u, 0u);
            const auto putU32 = [&](std::size_t offset, std::uint32_t value)
            {
                for (int byte = 0; byte < 4; ++byte)
                {
                    loop[offset + static_cast<std::size_t>(byte)] =
                        static_cast<std::uint8_t>(value >> (byte * 8));
                }
            };
            putU32(28u, 1u);
            putU32(44u, loopStart);
            putU32(48u, loopEnd);
        }

        const std::uint16_t blockAlign =
            static_cast<std::uint16_t>(channels * (bitsPerSample / 8u));
        const std::uint32_t riffSize =
            4u + 24u + 8u + static_cast<std::uint32_t>(samples.size()) +
            (loop.empty() ? 0u : 8u + static_cast<std::uint32_t>(loop.size()));
        writeTag("RIFF");
        writeU32(riffSize);
        writeTag("WAVE");
        writeTag("fmt ");
        writeU32(16u);
        writeU16(1u);
        writeU16(channels);
        writeU32(sampleRate);
        writeU32(sampleRate * blockAlign);
        writeU16(blockAlign);
        writeU16(bitsPerSample);
        writeTag("data");
        writeU32(static_cast<std::uint32_t>(samples.size()));
        output.insert(output.end(), samples.begin(), samples.end());
        if (!loop.empty())
        {
            writeTag("smpl");
            writeU32(static_cast<std::uint32_t>(loop.size()));
            output.insert(output.end(), loop.begin(), loop.end());
        }
        return output;
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterSoundEffectContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentBuildResult BuildSound(
        const std::filesystem::path& root,
        const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "explosion.wav";
        request.logicalName = "Sounds/explosion";
        request.parameters = parameters;
        return pipeline.Build(request);
    }

#if !defined(_WIN32)
    int RunSourceTool(const std::vector<std::string>& arguments)
    {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_SOURCE_TO_CNB_TOOL_PATH));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        if (posix_spawn(&pid, CNA_SOURCE_TO_CNB_TOOL_PATH, nullptr, nullptr, argv.data(), environ) !=
            0)
        {
            return -1;
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { return -1; }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif
}

TEST(SoundEffectContentPipelineTest, ImportPreservesSourceEncodingUntilTheProcessor)
{
    const std::vector<std::uint8_t> pcm8{0u, 128u, 255u, 64u};
    const std::vector<std::uint8_t> wav = MakeWav(1u, 8000u, 8u, pcm8);
    const Import::ImportedSound imported = Cnb::DecodeWavAsImportedSound(wav, "eight.wav");
    EXPECT_EQ(imported.encoding, Import::ImportedPcmEncoding::Unsigned8);
    EXPECT_EQ(imported.samples, pcm8);
    EXPECT_EQ(imported.frameCount, 4u);

    const Cnb::CnbSoundEffectData processed = Cnb::ProcessImportedSoundEffect(imported);
    ASSERT_EQ(processed.samples.size(), 8u);
    const auto sample = [&](std::size_t index)
    {
        return static_cast<std::int16_t>(
            processed.samples[index * 2u] | (processed.samples[index * 2u + 1u] << 8u));
    };
    EXPECT_EQ(sample(0u), static_cast<std::int16_t>(-32768));
    EXPECT_EQ(sample(1u), 0);
    EXPECT_EQ(sample(2u), static_cast<std::int16_t>(32512));
    EXPECT_EQ(sample(3u), static_cast<std::int16_t>(-16384));
}

TEST(SoundEffectContentPipelineTest, IsDeterministicAndByteIdenticalToTheExistingProducer)
{
    ScratchDirectory scratch("oracle");
    const std::filesystem::path source = scratch.Path() / "explosion.wav";
    const std::vector<std::uint8_t> pcm = MakePcm16(250u, 2u);
    WriteBytes(source, MakeWav(2u, 48000u, 16u, pcm, true, 20u, 100u));

    const Pipeline::ContentBuildResult first = BuildSound(scratch.Path());
    const Pipeline::ContentBuildResult second = BuildSound(scratch.Path());
    EXPECT_EQ(first.output.bytes, second.output.bytes);
    EXPECT_EQ(first.importer, (Pipeline::ContentComponentIdentity{"CNA.WavImporter", "1"}));
    EXPECT_EQ(first.processor,
              (Pipeline::ContentComponentIdentity{"CNA.SoundEffectProcessor", "1"}));
    ASSERT_EQ(first.messages.size(), 2u);
    EXPECT_EQ(first.messages[0].stage, Pipeline::ContentPipelineStage::Import);
    EXPECT_EQ(first.messages[0].component, "CNA.WavImporter");
    EXPECT_EQ(first.messages[1].stage, Pipeline::ContentPipelineStage::Process);
    EXPECT_EQ(first.messages[1].component, "CNA.SoundEffectProcessor");
    EXPECT_EQ(first.writer,
              (Pipeline::ContentComponentIdentity{"CNA.SoundEffectContentWriter", "1"}));
    EXPECT_EQ(first.output.assetTypeId, Cnb::CnbAssetTypeId::SoundEffect);
    ASSERT_EQ(first.dependencies.size(), 1u);
    EXPECT_EQ(first.dependencies[0].kind, Pipeline::ContentDependencyKind::PrimarySource);
    EXPECT_TRUE(first.runtimeReferences.empty());

    const Cnb::CnbSoundEffectData oldData = Cnb::ImportWavAsCnbSoundEffect(source.string());
    const std::vector<std::uint8_t> oldLibraryBytes =
        Cnb::EncodeSoundEffectToCnb(oldData, "Sounds/explosion");
    EXPECT_EQ(first.output.bytes, oldLibraryBytes);

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool.cnb";
    ASSERT_EQ(RunSourceTool(
                  {source.string(), oldToolOutput.string(), "--name", "Sounds/explosion"}),
              0);
    EXPECT_EQ(first.output.bytes, ReadBytes(oldToolOutput));
#endif

    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(first.output.bytes, "pipeline explosion.cnb");
    EXPECT_EQ(document.Metadata().contentName, "Sounds/explosion");
    const Cnb::CnbSoundEffectData decoded = Cnb::DecodeSoundEffectFromCnb(document);
    EXPECT_EQ(decoded.sampleRate, 48000u);
    EXPECT_EQ(decoded.channels, 2u);
    EXPECT_EQ(decoded.frameCount, 250u);
    EXPECT_EQ(decoded.loopStart, 20u);
    EXPECT_EQ(decoded.loopLength, 80u);
    EXPECT_EQ(decoded.samples, pcm);
}

TEST(SoundEffectContentPipelineTest, ReadsANativeNonAsciiFilesystemPathWithoutNarrowing)
{
    ScratchDirectory scratch("unicode");
    const std::filesystem::path directory =
        scratch.Path() / std::filesystem::path(u8"Zvuky");
    std::filesystem::create_directories(directory);
    const std::filesystem::path source =
        directory / std::filesystem::path(u8"výbuch_音.wav");
    WriteBytes(source, MakeWav(1u, 22050u, 16u, MakePcm16(20u, 1u)));

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = source;
    request.logicalName = "Zvuky/výbuch_音";
    const Pipeline::ContentBuildResult result = pipeline.Build(request);

    ASSERT_EQ(result.dependencies.size(), 1u);
    EXPECT_EQ(result.dependencies.front().identity,
              CNA::Internal::ContentPathToUtf8(std::filesystem::weakly_canonical(source)));
    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(result.output.bytes, "unicode WAV pipeline output");
    EXPECT_EQ(document.Metadata().contentName, "Zvuky/výbuch_音");
    EXPECT_EQ(Cnb::DecodeSoundEffectFromCnb(document).frameCount, 20u);

    const Cnb::CnbSoundEffectData compatibility = Cnb::ImportWavAsCnbSoundEffect(source);
    EXPECT_EQ(Cnb::EncodeSoundEffectToCnb(compatibility, request.logicalName), result.output.bytes);
}

TEST(SoundEffectContentPipelineTest, EightBitPipelineBytesMatchTheExistingProducer)
{
    ScratchDirectory scratch("eight");
    const std::filesystem::path source = scratch.Path() / "explosion.wav";
    WriteBytes(source, MakeWav(1u, 11025u, 8u, {0u, 64u, 128u, 192u, 255u}));

    const Pipeline::ContentBuildResult result = BuildSound(scratch.Path());
    const std::vector<std::uint8_t> oldBytes = Cnb::EncodeSoundEffectToCnb(
        Cnb::ImportWavAsCnbSoundEffect(source.string()), "Sounds/explosion");
    EXPECT_EQ(result.output.bytes, oldBytes);
}

TEST(SoundEffectContentPipelineTest, ProcessingRejectsAnInconsistentImportedSound)
{
    Import::ImportedSound imported;
    imported.encoding = Import::ImportedPcmEncoding::Signed16LittleEndian;
    imported.sampleRate = 44100u;
    imported.channels = 1u;
    imported.frameCount = 2u;
    imported.samples = {0u, 0u};
    EXPECT_THROW(static_cast<void>(Cnb::ProcessImportedSoundEffect(imported)),
                 Microsoft::Xna::Framework::Content::ContentLoadException);

    imported.samples = {0u, 0u, 0u, 0u};
    imported.encoding = static_cast<Import::ImportedPcmEncoding>(99);
    EXPECT_THROW(static_cast<void>(Cnb::ProcessImportedSoundEffect(imported)),
                 Microsoft::Xna::Framework::Content::ContentLoadException);
}

TEST(SoundEffectContentPipelineTest, RejectsParametersAtTheProcessorBoundary)
{
    ScratchDirectory scratch("parameters");
    WriteBytes(scratch.Path() / "explosion.wav",
               MakeWav(1u, 22050u, 16u, MakePcm16(10u, 1u)));
    Pipeline::ContentProcessorParameters parameters;
    parameters.Set("resampleRate", std::uint64_t{44100u});

    try
    {
        static_cast<void>(BuildSound(scratch.Path(), parameters));
        FAIL() << "unknown SoundEffectProcessor parameter should fail";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
        EXPECT_EQ(error.Component(), "CNA.SoundEffectProcessor");
    }
}

TEST(SoundEffectContentPipelineTest, MalformedWavReportsTheImporterStageAndComponent)
{
    ScratchDirectory scratch("malformed");
    WriteBytes(scratch.Path() / "explosion.wav", std::vector<std::uint8_t>(10u, 0u));

    try
    {
        static_cast<void>(BuildSound(scratch.Path()));
        FAIL() << "malformed WAV should fail";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_EQ(error.Component(), "CNA.WavImporter");
        EXPECT_NE(std::string(error.what()).find("too short"), std::string::npos);
    }
}
