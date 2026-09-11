// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-201, XNAPP-202, XNAPP-220, XNAPP-136, XNAPP-043: the three
// source formats XNA reads through Windows Media, and the video family they need.
//
// The expectations are tests/reference/xna40/media/media-content-oracle.json, measured black-box
// from the genuine assemblies over the same committed corpus these tests read
// (tests/assets/xna40/media). What could and could not be measured in this environment is
// docs/xna-content-pipeline-media.md section 6, and this file says at each case which it is
// leaning on -- a measured answer or the format itself.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Pipeline/BuildTimeMediaDecoder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Mp3Importer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/AudioProcessors.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/VideoProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/WavImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/WmaImporter.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/NotSupportedException.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;
using Xna::InvalidContentException;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    std::filesystem::path Fixture(const std::string& name)
    {
        return Locate("tests/assets/xna40/media") / name;
    }

    std::string Unescape(const std::string& text)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char next = text[++i];
                out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            }
            else
            {
                out += text[i];
            }
        }
        return out;
    }

    std::string Expected(const std::string& name)
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(Locate("tests/reference/xna40/media/media-content-oracle.json"));
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = Unescape(match[2]);
                }
            }
            return map;
        }();
        const auto found = cases.find(name);
        return found == cases.end() ? std::string("<missing case ") + name + ">" : found->second;
    }

    /** @brief One `name=value` field out of a corpus record. */
    std::string Field(const std::string& record, const std::string& name)
    {
        const std::regex pattern(name + "=([^ ]*)");
        std::smatch match;
        if (!std::regex_search(record, match, pattern))
        {
            return std::string();
        }
        // The audio format's own description opens with "format=", so a field of that name
        // matches twice; the value is what follows the last one.
        std::string value = match[1].str();
        const std::size_t again = value.rfind(name + "=");
        return again == std::string::npos ? value : value.substr(again + name.size() + 1);
    }

    class ImporterContext final : public Xna::ContentImporterContext
    {
    public:
        std::vector<std::string> dependencies;
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override
        {
            return const_cast<SilentLogger&>(logger_);
        }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        void AddDependency(const std::string& filename) override { dependencies.push_back(filename); }

    private:
        class SilentLogger final : public Xna::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string&) override {}
            void LogImportantMessage(const std::string&) override {}
            void LogWarning(const std::string&, const Xna::ContentIdentity&, const std::string&) override {}
        };

        SilentLogger logger_;
    };

    class ProcessorContext : public Xna::ContentProcessorContext
    {
    public:
        std::vector<std::string> dependencies;
        std::vector<std::string> outputFiles;
        [[nodiscard]] std::string getBuildConfigurationProperty() const override { return "Debug"; }
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override
        {
            return const_cast<SilentLogger&>(logger_);
        }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return "bin/asset.xnb"; }
        [[nodiscard]] const Xna::OpaqueDataDictionary& getParametersProperty() const override
        {
            return parameters_;
        }
        [[nodiscard]] Xna::TargetPlatform getTargetPlatformProperty() const override
        {
            return platform_;
        }

        /** @brief The target this context builds for; Windows unless a test says otherwise. */
        Xna::TargetPlatform platform_ = Xna::TargetPlatform::Windows;
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile
        getTargetProfileProperty() const override
        {
            return Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;
        }
        void AddDependency(const std::string& filename) override { dependencies.push_back(filename); }
        void AddOutputFile(const std::string& filename) override { outputFiles.push_back(filename); }

    protected:
        [[nodiscard]] Xna::ContentObject BuildAndLoadAssetCore(const std::string&, const Xna::ContentIdentity&,
                                                               const std::string&, const Xna::OpaqueDataDictionary&,
                                                               const std::string&, const std::string&,
                                                               const std::string&) override
        {
            throw System::NotSupportedException("BuildAndLoadAsset");
        }
        [[nodiscard]] std::string BuildAssetCore(const std::string&, const Xna::ContentIdentity&,
                                                 const std::string&, const Xna::OpaqueDataDictionary&,
                                                 const std::string&, const std::string&, const std::string&,
                                                 const std::string&) override
        {
            throw System::NotSupportedException("BuildAsset");
        }
        [[nodiscard]] Xna::ContentObject ConvertCore(const Xna::ContentObject&, const std::string&,
                                                     const Xna::OpaqueDataDictionary&, const std::string&,
                                                     const std::string&) override
        {
            throw System::NotSupportedException("Convert");
        }

    private:
        class SilentLogger final : public Xna::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string&) override {}
            void LogImportantMessage(const std::string&) override {}
            void LogWarning(const std::string&, const Xna::ContentIdentity&, const std::string&) override {}
        };

        SilentLogger logger_;
        Xna::OpaqueDataDictionary parameters_;
    };

    bool MediaAvailable()
    {
        return CNA::Content::Pipeline::BuildTimeMedia::IsAvailable();
    }
}

// Every MP3 in the corpus, against the format and duration the genuine importer answered for the
// same file. The rate is the finding: 44100 for every source from 8000 to 48000, across all three
// MPEG versions, with only the channel count surviving.
TEST(XnaMp3Importer, EveryMp3AnswersTheFormatAndDurationXnaAnswers)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    for (const std::string& name :
         {"mp3_mono_44100_128k.mp3", "mp3_stereo_44100_192k.mp3", "mp3_mono_22050_64k.mp3",
          "mp3_mono_44100_tagged.mp3", "mp3_mono_44100_vbr.mp3", "mp3_mono_48000_128k.mp3",
          "mp3_mono_32000_128k.mp3", "mp3_mono_24000_64k.mp3", "mp3_mono_16000_64k.mp3",
          "mp3_mono_8000_32k.mp3", "mp3_stereo_22050_96k.mp3", "mp3_mono_44100_2s.mp3"})
    {
        const std::string record = Expected("mp3/" + name);
        ASSERT_NE(record.find("IMPORT RETURNED"), std::string::npos) << name;

        ImporterContext context;
        Xna::Mp3Importer importer;
        const auto audio = importer.Import(Fixture(name).string(), context);
        ASSERT_NE(audio, nullptr) << name;
        EXPECT_EQ(audio->getFileTypeProperty(), Xna::Audio::AudioFileType::Mp3) << name;
        EXPECT_EQ(std::to_string(audio->getFormatProperty()->getFormatProperty()),
                  Field(record, "format")) << name;
        EXPECT_EQ(std::to_string(audio->getFormatProperty()->getChannelCountProperty()),
                  Field(record, "channels")) << name;
        EXPECT_EQ(std::to_string(audio->getFormatProperty()->getSampleRateProperty()),
                  Field(record, "sampleRate")) << name;
        EXPECT_EQ(std::to_string(audio->getFormatProperty()->getBitsPerSampleProperty()),
                  Field(record, "bits")) << name;
        EXPECT_EQ(std::to_string(audio->getFormatProperty()->getBlockAlignProperty()),
                  Field(record, "blockAlign")) << name;
        EXPECT_EQ(std::to_string(audio->getFormatProperty()->getAverageBytesPerSecondProperty()),
                  Field(record, "bytesPerSecond")) << name;
        // Both zero, where a WAV that names no loop answers 0 and its whole length.
        EXPECT_EQ(std::to_string(audio->getLoopStartProperty()), Field(record, "loopStart")) << name;
        EXPECT_EQ(std::to_string(audio->getLoopLengthProperty()), Field(record, "loopLength")) << name;
        // The duration is whole milliseconds and counts the encoder delay and padding the file
        // carries, so a half-second tone is 548 ms and not 500. Two decoders can disagree by a
        // frame about where a stream ends, so the comparison is to the millisecond it rounds to
        // plus a frame's worth on either side.
        // The record carries ticks; the milliseconds are what both sides truncate to.
        const std::int64_t expectedMs = std::stoll(Field(record, "durationTicks")) / 10000;
        const std::int64_t actualMs = audio->getDurationProperty().getTicksProperty() / 10000;
        EXPECT_LE(std::abs(actualMs - expectedMs), 30)
            << name << ": CNA " << actualMs << " ms, XNA " << expectedMs << " ms";
        EXPECT_TRUE(context.dependencies.empty()) << name;
        EXPECT_TRUE(audio->getIdentityProperty().IsEmpty()) << name;
        // The samples themselves: the format says 16-bit at the reported rate, so the byte count
        // has to agree with the duration the same object reports.
        const std::size_t frames = audio->getDataProperty().size() /
                                   static_cast<std::size_t>(audio->getFormatProperty()->getBlockAlignProperty());
        EXPECT_GT(frames, 0u) << name;
        EXPECT_LE(std::abs(static_cast<std::int64_t>(frames * 1000 /
                                                     static_cast<std::size_t>(
                                                         audio->getFormatProperty()->getSampleRateProperty())) -
                           actualMs),
                  2)
            << name << ": the samples and the duration disagree";
    }
}

// The refusals, each the sentence the genuine importer gave for the same file.
TEST(XnaMp3Importer, RefusalsMatchXna)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    ImporterContext context;
    Xna::Mp3Importer importer;

    // A file that is not there is the runtime's own exception, and its message keeps XNA's
    // unformatted placeholder.
    EXPECT_EQ(Expected("mp3/missing.mp3"),
              "throws FileNotFoundException: Could not locate audio file \"{0}\".");
    EXPECT_THROW((void)importer.Import(Fixture("missing.mp3").string(), context),
                 System::IO::FileNotFoundException);

    // Zero bytes, a truncated file, text, and a WAV wearing the wrong extension: the importers are
    // content-driven, so all four are refused with the one open-failure sentence.
    for (const std::string& name : {"empty.mp3", "garbage.mp3", "actually_wav.mp3"})
    {
        const std::string record = Expected("mp3/" + name);
        ASSERT_EQ(record.rfind("throws InvalidContentException: ", 0), 0u) << name;
        const std::string message = record.substr(std::string("throws InvalidContentException: ").size());
        try
        {
            (void)importer.Import(Fixture(name).string(), context);
            ADD_FAILURE() << name << " was accepted";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), message) << name;
        }
    }

    // And the other way round: an MP3 named .wav is refused by WavImporter with the same sentence.
    Xna::WavImporter wav;
    const std::string record = Expected("wav/actually_mp3.wav");
    const std::string message = record.substr(std::string("throws InvalidContentException: ").size());
    try
    {
        (void)wav.Import(Fixture("actually_mp3.wav").string(), context);
        ADD_FAILURE() << "an MP3 named .wav was accepted as a WAV";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_EQ(error.getMessageProperty(), message);
    }
}

// WMA: the genuine importer could not be asked in this environment (Wine has no Windows Media
// Format runtime, docs/xna-content-pipeline-media.md section 6), so these hold the route to the
// format itself and to the shape the MP3 measurement settled for the same SongProcessor input.
TEST(XnaWmaImporter, EveryWmaReadsAsTheDecodedPcmTheSongPathNeeds)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    struct Case
    {
        std::string name;
        int channels;
        int milliseconds;
    };
    for (const Case& one : std::vector<Case>{{"wma_mono_44100.wma", 1, 500},
                                             {"wma_stereo_44100.wma", 2, 500},
                                             {"wma_mono_22050.wma", 1, 500},
                                             {"wma_v1_mono_44100.wma", 1, 500}})
    {
        ImporterContext context;
        Xna::WmaImporter importer;
        const auto audio = importer.Import(Fixture(one.name).string(), context);
        ASSERT_NE(audio, nullptr) << one.name;
        EXPECT_EQ(audio->getFileTypeProperty(), Xna::Audio::AudioFileType::Wma) << one.name;
        EXPECT_EQ(audio->getFormatProperty()->getFormatProperty(), 1) << one.name;
        EXPECT_EQ(audio->getFormatProperty()->getChannelCountProperty(), one.channels) << one.name;
        EXPECT_EQ(audio->getFormatProperty()->getSampleRateProperty(), 44100) << one.name;
        EXPECT_EQ(audio->getFormatProperty()->getBitsPerSampleProperty(), 16) << one.name;
        EXPECT_EQ(audio->getLoopStartProperty(), 0) << one.name;
        EXPECT_EQ(audio->getLoopLengthProperty(), 0) << one.name;
        const std::int64_t ms = audio->getDurationProperty().getTicksProperty() / 10000;
        EXPECT_NEAR(static_cast<double>(ms), one.milliseconds, 120.0) << one.name;
        EXPECT_FALSE(audio->getDataProperty().empty()) << one.name;
    }
}

TEST(XnaWmaImporter, RefusalsMatchXna)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    ImporterContext context;
    Xna::WmaImporter importer;
    EXPECT_EQ(Expected("wma/missing.wma"),
              "throws FileNotFoundException: Could not locate audio file \"{0}\".");
    EXPECT_THROW((void)importer.Import(Fixture("missing.wma").string(), context),
                 System::IO::FileNotFoundException);
    for (const std::string& name : {"empty.wma", "truncated.wma"})
    {
        try
        {
            (void)importer.Import(Fixture(name).string(), context);
            ADD_FAILURE() << name << " was accepted";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getMessageProperty(),
                      "Failed to open file " + name +
                          ". Ensure the file is a valid audio file and is not DRM protected.")
                << name;
        }
    }
}

// VideoContent's constructor is eager, and every refusal is the one sentence.
TEST(XnaVideoContent, EveryRefusalIsTheOneSentenceXnaGives)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    const auto sentence = [](const std::string& name)
    {
        return "Video file " + name +
               " is invalid. Please make sure that the video is not DRM protected and is a valid "
               "single-pass CBR encoded video file.";
    };
    // The exact strings the genuine constructor answered for the same four cases.
    EXPECT_EQ(Expected("videocontent/construct_missing"),
              "throws InvalidContentException: " + sentence("missing.wmv"));
    EXPECT_EQ(Expected("videocontent/construct_empty"),
              "throws InvalidContentException: " + sentence(""));
    EXPECT_EQ(Expected("videocontent/construct_null"),
              "throws InvalidContentException: " + sentence(""));
    EXPECT_EQ(Expected("videocontent/construct_not_video"),
              "throws InvalidContentException: " + sentence("tone_mono_44100.wav"));

    const auto refuses = [&sentence](const std::string& path, const std::string& named)
    {
        try
        {
            Xna::VideoContent video(path);
            ADD_FAILURE() << path << " was accepted";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), sentence(named)) << path;
        }
    };
    refuses(Fixture("missing.wmv").string(), "missing.wmv");
    // A missing file through the constructor is NOT a FileNotFoundException; that is the
    // importer's check and not this one's.
    refuses("", "");
    refuses(Fixture("tone_mono_44100.wav").string(), "tone_mono_44100.wav");
    refuses(Fixture("empty.wmv").string(), "empty.wmv");
    refuses(Fixture("truncated.wmv").string(), "truncated.wmv");
    refuses(Fixture("mp3_mono_44100_128k.mp3").string(), "mp3_mono_44100_128k.mp3");
}

TEST(XnaVideoContent, AReadableVideoAnswersItsOwnShape)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    Xna::VideoContent silent(Fixture("wmv_64x48_15fps_silent.wmv").string());
    EXPECT_EQ(silent.getWidthProperty(), 64);
    EXPECT_EQ(silent.getHeightProperty(), 48);
    EXPECT_NEAR(silent.getFramesPerSecondProperty(), 15.0f, 0.01f);
    EXPECT_GT(silent.getBitsPerSecondProperty(), 0);
    EXPECT_NEAR(static_cast<double>(silent.getDurationProperty().getTicksProperty() / 10000), 1000.0, 120.0);
    EXPECT_EQ(silent.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Music);
    EXPECT_FALSE(silent.HasSoundtrackEXT());
    EXPECT_EQ(silent.getFilenameProperty(), Fixture("wmv_64x48_15fps_silent.wmv").string());

    Xna::VideoContent sounded(Fixture("wmv_320x240_30fps_stereo.wmv").string());
    EXPECT_EQ(sounded.getWidthProperty(), 320);
    EXPECT_EQ(sounded.getHeightProperty(), 240);
    EXPECT_NEAR(sounded.getFramesPerSecondProperty(), 30.0f, 0.01f);
    EXPECT_TRUE(sounded.HasSoundtrackEXT());

    // Every property keeps answering after Dispose, and a second Dispose is accepted.
    sounded.Dispose();
    sounded.Dispose();
    EXPECT_EQ(sounded.getWidthProperty(), 320);
    EXPECT_EQ(sounded.getFilenameProperty(), Fixture("wmv_320x240_30fps_stereo.wmv").string());
}

TEST(XnaWmvImporter, ImportAndItsRefusalsMatchXna)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    ImporterContext context;
    Xna::WmvImporter importer;
    const auto video = importer.Import(Fixture("wmv_64x48_15fps_silent.wmv").string(), context);
    ASSERT_NE(video, nullptr);
    EXPECT_EQ(video->getWidthProperty(), 64);
    EXPECT_TRUE(context.dependencies.empty());

    // Where the constructor gives its one sentence, the importer gives the runtime's exception,
    // with XNA's unformatted placeholder.
    EXPECT_EQ(Expected("wmv/missing.wmv"),
              "throws FileNotFoundException: Could not locate video file \"{0}\".");
    EXPECT_THROW((void)importer.Import(Fixture("missing.wmv").string(), context),
                 System::IO::FileNotFoundException);

    EXPECT_EQ(Xna::WmvImporter::Attribute().getFileExtensionsProperty(),
              std::vector<std::string>{".wmv"});
    EXPECT_EQ(Xna::WmvImporter::Attribute().getDisplayNameProperty(), "WMV Video File - XNA Framework");
    EXPECT_EQ(Xna::WmvImporter::Attribute().getDefaultProcessorProperty(), "VideoProcessor");
    EXPECT_FALSE(Xna::WmvImporter::Attribute().getCacheImportedDataProperty());
}

TEST(XnaVideoProcessor, DefaultsAndProcessMatchXna)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    EXPECT_EQ(Expected("videoprocessor/defaults"), "VideoSoundtrackType=Music");
    Processors::VideoProcessor processor;
    EXPECT_EQ(processor.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Music);

    // The same object comes back, carrying the soundtrack type the processor was set to, and the
    // source is both a dependency and an output file because the built asset streams from it.
    for (const VideoSoundtrackType soundtrack :
         {VideoSoundtrackType::Music, VideoSoundtrackType::Dialog, VideoSoundtrackType::MusicAndDialog})
    {
        ImporterContext importing;
        ProcessorContext context;
        Xna::WmvImporter importer;
        const auto input = importer.Import(Fixture("wmv_320x240_30fps_stereo.wmv").string(), importing);
        Processors::VideoProcessor configured;
        configured.setVideoSoundtrackTypeProperty(soundtrack);
        const auto output = configured.Process(input, context);
        EXPECT_EQ(output, input) << "the processor answers its own input";
        EXPECT_EQ(output->getVideoSoundtrackTypeProperty(), soundtrack);
        ASSERT_EQ(context.dependencies.size(), 1u);
        ASSERT_EQ(context.outputFiles.size(), 1u);
        EXPECT_EQ(context.dependencies[0], input->getFilenameProperty());
        EXPECT_EQ(context.outputFiles[0], input->getFilenameProperty());
    }

    // A null input names the parameter, as XNA's does.
    EXPECT_EQ(Expected("videoprocessor/process_null"),
              "throws ArgumentNullException: Value cannot be null.\r\nParameter name: input");
    ProcessorContext context;
    EXPECT_THROW((void)processor.Process(nullptr, context), System::ArgumentNullException);
}

TEST(XnaVideoProcessor, TheEnumValuesMatchXna)
{
    // Music = 0, Dialog = 1, MusicAndDialog = 2 (read from the assemblies' metadata).
    EXPECT_EQ(static_cast<int>(VideoSoundtrackType::Music), 0);
    EXPECT_EQ(static_cast<int>(VideoSoundtrackType::Dialog), 1);
    EXPECT_EQ(static_cast<int>(VideoSoundtrackType::MusicAndDialog), 2);
}

TEST(XnaMediaImporters, EveryAttributeMatchesXna)
{
    EXPECT_EQ(Expected("attribute/mp3"),
              "extensions=[.mp3] displayName=MP3 Audio File - XNA Framework "
              "defaultProcessor=SongProcessor cacheImportedData=False");
    EXPECT_EQ(Xna::Mp3Importer::Attribute().getFileExtensionsProperty(), std::vector<std::string>{".mp3"});
    EXPECT_EQ(Xna::Mp3Importer::Attribute().getDisplayNameProperty(), "MP3 Audio File - XNA Framework");
    EXPECT_EQ(Xna::Mp3Importer::Attribute().getDefaultProcessorProperty(), "SongProcessor");
    EXPECT_FALSE(Xna::Mp3Importer::Attribute().getCacheImportedDataProperty());

    EXPECT_EQ(Expected("attribute/wma"),
              "extensions=[.wma] displayName=WMA Audio File - XNA Framework "
              "defaultProcessor=SongProcessor cacheImportedData=False");
    EXPECT_EQ(Xna::WmaImporter::Attribute().getFileExtensionsProperty(), std::vector<std::string>{".wma"});
    EXPECT_EQ(Xna::WmaImporter::Attribute().getDisplayNameProperty(), "WMA Audio File - XNA Framework");
    EXPECT_EQ(Xna::WmaImporter::Attribute().getDefaultProcessorProperty(), "SongProcessor");
    EXPECT_FALSE(Xna::WmaImporter::Attribute().getCacheImportedDataProperty());
}

// ---- XNAPP-161, XNAPP-136: the song a SongProcessor writes ------------------------------------
//
// The previous handoff recorded SongProcessor as EXTERNAL_BLOCKED, because Microsoft's Windows
// Media encoder exists only on the platform that owns it and XNA's own never returns under the
// oracle's Wine prefix. That is true of Microsoft's encoder and not of the format: a song is a
// Windows Media file the runtime streams, and one can be written here. What these hold is the
// shape -- a real WMA beside the output asset, named after it, whose samples read back -- and the
// refusal text, rather than Microsoft's exact bytes, which nothing here could compare against.

TEST(XnaSongProcessor, ASongIsWrittenAsRealWindowsMediaAudioBesideTheAsset)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "cna_xnapp161_song";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);

    ImporterContext importing;
    Xna::Mp3Importer importer;
    const auto audio = importer.Import(Fixture("mp3_stereo_44100_192k.mp3").string(), importing);
    const System::TimeSpan sourceDuration = audio->getDurationProperty();

    class OutputContext final : public ProcessorContext
    {
    public:
        explicit OutputContext(std::string filename) : filename_(std::move(filename)) {}
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return filename_; }

    private:
        std::string filename_;
    };

    OutputContext context((scratch / "Theme.xnb").string());
    Processors::SongProcessor processor;
    const auto song = processor.Process(audio, context);
    ASSERT_NE(song, nullptr);

    // The .xnb names a file beside it; the content carries the name and the duration, which is
    // everything XNA's SongContent has (it declares no public member of its own).
    EXPECT_EQ(song->FileName(), "Theme.wma");
    EXPECT_EQ(song->Duration().getTicksProperty(), sourceDuration.getTicksProperty());
    const std::filesystem::path written = scratch / "Theme.wma";
    ASSERT_TRUE(std::filesystem::exists(written));
    EXPECT_GT(std::filesystem::file_size(written), 0u);
    ASSERT_EQ(context.outputFiles.size(), 1u);
    EXPECT_EQ(context.outputFiles[0], written.string());

    // And it is really Windows Media audio, not a renamed something: reading it back through the
    // WMA route -- which refuses anything that is not WMA -- is the check that means it.
    ImporterContext reading;
    Xna::WmaImporter wma;
    const auto readBack = wma.Import(written.string(), reading);
    ASSERT_NE(readBack, nullptr);
    EXPECT_EQ(readBack->getFormatProperty()->getChannelCountProperty(), 2);
    EXPECT_EQ(readBack->getFormatProperty()->getSampleRateProperty(), 44100);
    // A lossy round trip does not preserve the length to the sample, but it must not lose or
    // invent a large part of it.
    const std::int64_t sourceMs = sourceDuration.getTicksProperty() / 10000;
    const std::int64_t readMs = readBack->getDurationProperty().getTicksProperty() / 10000;
    EXPECT_NEAR(static_cast<double>(readMs), static_cast<double>(sourceMs), 200.0);

    std::filesystem::remove_all(scratch);
}

TEST(XnaSongProcessor, TheQualityChoosesTheBitRate)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "cna_xnapp161_song_quality";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);

    class OutputContext final : public ProcessorContext
    {
    public:
        explicit OutputContext(std::string filename) : filename_(std::move(filename)) {}
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return filename_; }

    private:
        std::string filename_;
    };

    std::uintmax_t previous = 0;
    // Best, then Medium, then Low: each writes a smaller file than the one before it, which is
    // what "quality" means for an encoder that keeps the same samples.
    for (const auto& [label, quality] :
         std::vector<std::pair<std::string, Xna::Audio::ConversionQuality>>{
             {"best", Xna::Audio::ConversionQuality::Best},
             {"medium", Xna::Audio::ConversionQuality::Medium},
             {"low", Xna::Audio::ConversionQuality::Low}})
    {
        ImporterContext importing;
        Xna::Mp3Importer importer;
        const auto audio = importer.Import(Fixture("mp3_mono_44100_2s.mp3").string(), importing);
        OutputContext context((scratch / (label + ".xnb")).string());
        Processors::SongProcessor processor;
        processor.setQualityProperty(quality);
        const auto song = processor.Process(audio, context);
        ASSERT_NE(song, nullptr) << label;
        const std::uintmax_t size = std::filesystem::file_size(scratch / (label + ".wma"));
        EXPECT_GT(size, 0u) << label;
        if (previous != 0)
        {
            EXPECT_LT(size, previous) << label << " is not smaller than the quality above it";
        }
        previous = size;
    }
    std::filesystem::remove_all(scratch);
}

// plans/plan_xnapipeline_parity.md XNAPP-021, Phase 13: the video and song target legs.
//
// XNA's own answer for these two is not measurable in this environment and the recorded corpus
// says so rather than leaving it blank: every `videoprocessor/target_*` case is
// `SEHException: External component has thrown an exception`, because constructing a VideoContent
// needs Media Foundation and Wine 10.0 aborts on `mfplat.dll.MFCreateVideoMediaType`
// (docs/xna-content-pipeline-media.md). XNA's Windows Media *encoder*, which SongProcessor needs,
// never returns under the same prefix.
//
// What can be held here is CNA's own half: both processors answer the same thing for every target,
// so a change that made either of them target-dependent would have to be a deliberate one, taken
// against a measurement that does not exist yet. That is a weaker claim than the texture, model,
// font and sound-effect legs make, and it is deliberately written as the weaker claim.
TEST(XnaVideoProcessor, TheAnswerIsTheSameForEveryTargetXnaHas)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    for (const char* recorded :
         {"videoprocessor/target_windows", "videoprocessor/target_xbox360",
          "videoprocessor/target_windowsphone"})
    {
        EXPECT_NE(Expected(recorded).find("SEHException"), std::string::npos)
            << recorded << ": if XNA's answer became measurable here, this leg should become a "
                           "differential rather than a same-answer check";
    }

    std::vector<std::string> answers;
    for (const Xna::TargetPlatform platform :
         {Xna::TargetPlatform::Windows, Xna::TargetPlatform::Xbox360,
          Xna::TargetPlatform::WindowsPhone})
    {
        ImporterContext importing;
        ProcessorContext context;
        context.platform_ = platform;
        Xna::WmvImporter importer;
        const auto input =
            importer.Import(Fixture("wmv_64x48_15fps_silent.wmv").string(), importing);
        Processors::VideoProcessor processor;
        const auto output = processor.Process(input, context);
        ASSERT_NE(output, nullptr);
        answers.push_back(std::to_string(output->getWidthProperty()) + "x" +
                          std::to_string(output->getHeightProperty()) + " " +
                          std::to_string(output->getFramesPerSecondProperty()) + " soundtrack=" +
                          std::to_string(static_cast<int>(output->getVideoSoundtrackTypeProperty())) +
                          " dependencies=" + std::to_string(context.dependencies.size()) +
                          " outputs=" + std::to_string(context.outputFiles.size()));
    }
    ASSERT_EQ(answers.size(), 3u);
    EXPECT_EQ(answers[0], answers[1]);
    EXPECT_EQ(answers[0], answers[2]);
}

// The song route's target leg, on the same footing as the video one and for the same reason:
// XNA's SongProcessor cannot be measured here at all. Both recorded cases are refusals from the
// Windows Media layer under Wine (`songprocessor/mp3` and `songprocessor/wma`), so there is no
// per-target answer to compare against and one cannot be invented. What is held is that CNA's own
// processor answers the same thing for every target XNA has.
TEST(XnaSongProcessor, TheAnswerIsTheSameForEveryTargetXnaHas)
{
    if (!MediaAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    for (const char* recorded : {"songprocessor/mp3", "songprocessor/wma"})
    {
        EXPECT_NE(Expected(recorded).find("throws"), std::string::npos)
            << recorded << ": if XNA's answer became measurable here, this leg should become a "
                           "differential rather than a same-answer check";
    }

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "cna_xnapp021_song_targets";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);

    class OutputContext final : public ProcessorContext
    {
    public:
        explicit OutputContext(std::string filename) : filename_(std::move(filename)) {}
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return filename_; }

    private:
        std::string filename_;
    };

    std::vector<std::string> answers;
    int index = 0;
    for (const Xna::TargetPlatform platform :
         {Xna::TargetPlatform::Windows, Xna::TargetPlatform::Xbox360,
          Xna::TargetPlatform::WindowsPhone})
    {
        ImporterContext importing;
        Xna::Mp3Importer importer;
        const auto audio =
            importer.Import(Fixture("mp3_mono_44100_128k.mp3").string(), importing);
        OutputContext context((scratch / ("Theme" + std::to_string(index++) + ".xnb")).string());
        context.platform_ = platform;
        Processors::SongProcessor processor;
        const auto song = processor.Process(audio, context);
        ASSERT_NE(song, nullptr);
        answers.push_back(std::to_string(song->Duration().getTicksProperty()) +
                          " outputs=" + std::to_string(context.outputFiles.size()));
    }
    ASSERT_EQ(answers.size(), 3u);
    EXPECT_EQ(answers[0], answers[1]);
    EXPECT_EQ(answers[0], answers[2]);
    std::error_code error;
    std::filesystem::remove_all(scratch, error);
}
