// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-160: the audio intermediate types -- AudioContent,
// AudioFormat and the three enumerations -- against what the genuine XNA 4.0 pipeline answers for
// the same WAV files (tests/reference/xna40/audio/audio-content-oracle.json, cases enums/*,
// audiocontent/* and refusals/*).
//
// The test writes the oracle's own WAV files byte for byte, so the numbers compared are the
// numbers XNA answered. What the measurements settle: the duration is whole milliseconds with the
// remainder dropped; the loop spans the whole sound when the file names no loop; the format is an
// eighteen-byte WAVEFORMATEX even for PCM; every unreadable file is refused with one message that
// names the file; and after Dispose the samples are gone while the format, the file name and the
// duration keep answering.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"

namespace Audio = Microsoft::Xna::Framework::Content::Pipeline::Audio;
using Audio::AudioContent;
using Audio::AudioFileType;
using Audio::AudioFormat;
using Audio::ConversionFormat;
using Audio::ConversionQuality;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;

namespace
{
    std::filesystem::path CorpusFile()
    {
        const std::filesystem::path relative = "tests/reference/xna40/audio/audio-content-oracle.json";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
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

    std::string Normalize(const std::string& result)
    {
        std::string text = result;
        const std::size_t parameter = text.find("Parameter name:");
        if (parameter != std::string::npos)
        {
            std::size_t cut = parameter;
            while (cut > 0 && (text[cut - 1] == '\n' || text[cut - 1] == '\r'))
            {
                --cut;
            }
            text = text.substr(0, cut);
        }
        const std::size_t core = text.find(" (Parameter '");
        if (core != std::string::npos)
        {
            const std::size_t end = text.find(')', core);
            text = text.substr(0, core) + (end == std::string::npos ? "" : text.substr(end + 1));
        }
        return text;
    }

    const std::map<std::string, std::string>& Oracle()
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(CorpusFile());
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
        return cases;
    }

    std::string Expected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : Normalize(found->second);
    }

    std::string Result(const std::function<std::string()>& body)
    {
        try
        {
            return Normalize(body());
        }
        catch (const System::Collections::Generic::KeyNotFoundException& error)
        {
            return Normalize("throws KeyNotFoundException: " + std::string(error.what()));
        }
        catch (const System::InvalidOperationException& error)
        {
            return Normalize("throws InvalidOperationException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            return Normalize("throws ArgumentOutOfRangeException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentException& error)
        {
            return Normalize("throws ArgumentException: " + error.getMessageProperty());
        }
        catch (const InvalidContentException& error)
        {
            return Normalize("throws InvalidContentException: " + error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return Normalize("throws Exception: " + error.getMessageProperty());
        }
    }

    /** @brief A directory the test writes its WAV sources into and removes afterwards. */
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp160_" + tag + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    /** @brief The oracle's own WriteWav, reproduced byte for byte. */
    std::string WriteWav(const std::filesystem::path& directory, const std::string& name, int sampleRate,
                         int channels, int bitsPerSample, int frames)
    {
        const std::filesystem::path path = directory / name;
        const int blockAlign = channels * bitsPerSample / 8;
        const int dataBytes = frames * blockAlign;
        std::vector<std::uint8_t> bytes;
        const auto ascii = [&bytes](const char* text)
        {
            for (const char* at = text; *at != '\0'; ++at)
            {
                bytes.push_back(static_cast<std::uint8_t>(*at));
            }
        };
        const auto word32 = [&bytes](std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
            }
        };
        const auto word16 = [&bytes](std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        };
        ascii("RIFF");
        word32(static_cast<std::uint32_t>(36 + dataBytes));
        ascii("WAVE");
        ascii("fmt ");
        word32(16);
        word16(1);
        word16(static_cast<std::uint16_t>(channels));
        word32(static_cast<std::uint32_t>(sampleRate));
        word32(static_cast<std::uint32_t>(sampleRate * blockAlign));
        word16(static_cast<std::uint16_t>(blockAlign));
        word16(static_cast<std::uint16_t>(bitsPerSample));
        ascii("data");
        word32(static_cast<std::uint32_t>(dataBytes));
        for (int i = 0; i < dataBytes; ++i)
        {
            bytes.push_back(static_cast<std::uint8_t>((i * 7 + 13) & 0xFF));
        }
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path.string();
    }

    std::string Hex(const std::vector<SharpRuntime::bytecs>& bytes, std::size_t count)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string text;
        for (std::size_t i = 0; i < count && i < bytes.size(); ++i)
        {
            text += digits[(bytes[i] >> 4) & 0xF];
            text += digits[bytes[i] & 0xF];
        }
        return text;
    }

    /** @brief The oracle's Describe(AudioFormat), reproduced. */
    std::string Describe(const std::shared_ptr<AudioFormat>& format)
    {
        if (format == nullptr)
        {
            return "null";
        }
        return "format=" + std::to_string(format->getFormatProperty()) + " channels=" +
               std::to_string(format->getChannelCountProperty()) + " sampleRate=" +
               std::to_string(format->getSampleRateProperty()) + " bits=" +
               std::to_string(format->getBitsPerSampleProperty()) + " blockAlign=" +
               std::to_string(format->getBlockAlignProperty()) + " bytesPerSecond=" +
               std::to_string(format->getAverageBytesPerSecondProperty()) + " native=" +
               Hex(format->getNativeWaveFormatProperty(), format->getNativeWaveFormatProperty().size());
    }

    /** @brief The oracle's Describe(AudioContent), reproduced. */
    std::string Describe(const AudioContent& audio)
    {
        return "fileType=" + std::string(audio.getFileTypeProperty() == AudioFileType::Wav ? "Wav"
                                         : audio.getFileTypeProperty() == AudioFileType::Mp3 ? "Mp3" : "Wma") +
               " duration=" + std::to_string(audio.getDurationProperty().getTicksProperty()) + " loopStart=" +
               std::to_string(audio.getLoopStartProperty()) + " loopLength=" +
               std::to_string(audio.getLoopLengthProperty()) + " dataLength=" +
               std::to_string(audio.getDataProperty().size()) + " " + Describe(audio.getFormatProperty());
    }
}

TEST(XnaAudioEnums, ValuesMatchXna)
{
    EXPECT_EQ("Wav=" + std::to_string(static_cast<int>(AudioFileType::Wav)) + " Mp3=" +
                  std::to_string(static_cast<int>(AudioFileType::Mp3)) + " Wma=" +
                  std::to_string(static_cast<int>(AudioFileType::Wma)),
              Expected("enums/AudioFileType"));
    EXPECT_EQ("Pcm=" + std::to_string(static_cast<int>(ConversionFormat::Pcm)) + " Adpcm=" +
                  std::to_string(static_cast<int>(ConversionFormat::Adpcm)) + " WindowsMedia=" +
                  std::to_string(static_cast<int>(ConversionFormat::WindowsMedia)) + " Xma=" +
                  std::to_string(static_cast<int>(ConversionFormat::Xma)),
              Expected("enums/ConversionFormat"));
    EXPECT_EQ("Low=" + std::to_string(static_cast<int>(ConversionQuality::Low)) + " Medium=" +
                  std::to_string(static_cast<int>(ConversionQuality::Medium)) + " Best=" +
                  std::to_string(static_cast<int>(ConversionQuality::Best)),
              Expected("enums/ConversionQuality"));
}

TEST(XnaAudioContent, ReadsAWavAsXnaDoes)
{
    ScratchDirectory scratch("read");
    const std::string mono = WriteWav(scratch.Path(), "mono8k.wav", 8000, 1, 16, 800);
    const std::string stereo = WriteWav(scratch.Path(), "stereo44k.wav", 44100, 2, 16, 4410);
    const std::string eightBit = WriteWav(scratch.Path(), "mono8bit.wav", 22050, 1, 8, 2205);

    AudioContent monoAudio(mono, AudioFileType::Wav);
    EXPECT_EQ("fileName=" + std::filesystem::path(monoAudio.getFileNameProperty()).filename().string() + " " +
                  Describe(monoAudio) + " first16=" + Hex(monoAudio.getDataProperty(), 16),
              Expected("audiocontent/mono_pcm16"));

    AudioContent stereoAudio(stereo, AudioFileType::Wav);
    EXPECT_EQ(Describe(stereoAudio), Expected("audiocontent/stereo_pcm16"));

    AudioContent eightBitAudio(eightBit, AudioFileType::Wav);
    EXPECT_EQ(Describe(eightBitAudio), Expected("audiocontent/mono_pcm8"));

    EXPECT_EQ("name=" + std::string(monoAudio.getNameProperty().empty() ? "null" : "set") + " identity=" +
                  (monoAudio.getIdentityProperty().IsEmpty() ? "null" : "set") + " opaque=" +
                  std::to_string(monoAudio.getOpaqueDataProperty().getCountProperty()) +
                  " fileNameIsFullPath=" +
                  (std::filesystem::path(monoAudio.getFileNameProperty()).is_absolute() ? "True" : "False"),
              Expected("audiocontent/identity_and_name"));
}

TEST(XnaAudioContent, RefusalsMatchXna)
{
    ScratchDirectory scratch("refuse");
    const std::string mono = WriteWav(scratch.Path(), "mono8k.wav", 8000, 1, 16, 800);
    const std::string garbage = (scratch.Path() / "garbage.wav").string();
    {
        std::ofstream out(garbage, std::ios::binary);
        const std::vector<char> bytes = {1, 2, 3, 4, 5, 6, 7, 8};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    EXPECT_EQ(Result([&scratch]
                     {
                         AudioContent audio((scratch.Path() / "absent.wav").string(), AudioFileType::Wav);
                         return Describe(audio);
                     }),
              Expected("refusals/missing_file"));
    EXPECT_EQ(Result([&mono]
                     {
                         AudioContent audio(mono, AudioFileType::Mp3);
                         return Describe(audio);
                     }),
              Expected("refusals/wrong_file_type"));
    EXPECT_EQ(Result([]
                     {
                         AudioContent audio("", AudioFileType::Wav);
                         return Describe(audio);
                     }),
              Expected("refusals/null_file_name"));
    EXPECT_EQ(Result([&garbage]
                     {
                         AudioContent audio(garbage, AudioFileType::Wav);
                         return Describe(audio);
                     }),
              Expected("refusals/not_a_wav"));
}

TEST(XnaAudioContent, WhatSurvivesDisposeMatchesXna)
{
    ScratchDirectory scratch("dispose");
    const std::string mono = WriteWav(scratch.Path(), "mono8k.wav", 8000, 1, 16, 800);
    AudioContent audio(mono, AudioFileType::Wav);
    audio.Dispose();
    const auto probe = [](const std::function<std::string()>& body)
    {
        try
        {
            return body();
        }
        catch (const InvalidContentException&)
        {
            return std::string("InvalidContentException");
        }
    };
    std::string text = "data=" + probe([&audio] { return std::to_string(audio.getDataProperty().size()); });
    text += " duration=" + probe([&audio]
                                 { return std::to_string(audio.getDurationProperty().getTicksProperty()); });
    text += " format=" + probe([&audio]
                               { return std::string(audio.getFormatProperty() == nullptr ? "null" : "set"); });
    text += " fileName=" +
            probe([&audio] { return std::string(audio.getFileNameProperty().empty() ? "null" : "set"); });
    text += " convert=" + probe([&audio]
                                {
                                    audio.ConvertFormat(ConversionFormat::Pcm, ConversionQuality::Best, "");
                                    return std::string("accepted");
                                });
    text += " disposeTwice=" + probe([&audio]
                                     {
                                         audio.Dispose();
                                         return std::string("accepted");
                                     });
    EXPECT_EQ(text, Expected("refusals/after_dispose"));
}

TEST(XnaAudioContent, TheSamplesAreReadOnlyAndCountWhatXnaCounts)
{
    ScratchDirectory scratch("readonly");
    const std::string mono = WriteWav(scratch.Path(), "mono8k.wav", 8000, 1, 16, 800);
    const AudioContent audio(mono, AudioFileType::Wav);
    // XNA answers a ReadOnlyCollection<byte>; here the samples are a const reference to the
    // vector, which is what a read-only view is in C++. Only the count is comparable.
    const std::string expected = Expected("refusals/data_is_read_only");
    EXPECT_EQ("count=" + std::to_string(audio.getDataProperty().size()),
              expected.substr(expected.find("count=")));
    static_assert(std::is_const_v<std::remove_reference_t<decltype(audio.getDataProperty())>>,
                  "AudioContent::Data must not be writable through its accessor.");
}
