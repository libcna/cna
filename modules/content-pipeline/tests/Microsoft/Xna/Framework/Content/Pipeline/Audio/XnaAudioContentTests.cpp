// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-160 and 161: the audio intermediate types -- AudioContent,
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

#include <cmath>
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

#include "CNA/Internal/Audio/MsAdpcmEncoder.hpp"
#include "CNA/Internal/Audio/WavDecoder.hpp"
#include "CNA/Internal/Audio/WavWrapper.hpp"
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

    /** @brief A sixteen-bit sine WAV: the kind of signal ADPCM is meant to carry. */
    std::string WriteSineWav(const std::filesystem::path& directory, const std::string& name, int sampleRate,
                             int frames, double hertz)
    {
        const std::filesystem::path path = directory / name;
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
        const int dataBytes = frames * 2;
        ascii("RIFF");
        word32(static_cast<std::uint32_t>(36 + dataBytes));
        ascii("WAVE");
        ascii("fmt ");
        word32(16);
        word16(1);
        word16(1);
        word32(static_cast<std::uint32_t>(sampleRate));
        word32(static_cast<std::uint32_t>(sampleRate * 2));
        word16(2);
        word16(16);
        ascii("data");
        word32(static_cast<std::uint32_t>(dataBytes));
        for (int frame = 0; frame < frames; ++frame)
        {
            const double phase = 2.0 * 3.14159265358979323846 * hertz * frame / sampleRate;
            word16(static_cast<std::uint16_t>(static_cast<std::int16_t>(20000.0 * std::sin(phase))));
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


// ---- XNAPP-161: what ConvertFormat does ------------------------------------------------------

namespace
{
    /** @brief The format fields of a description, without the sample-dependent parts. */
    std::string ShapeOf(const std::string& described)
    {
        const std::size_t at = described.find("format=");
        return at == std::string::npos ? described : described.substr(at, described.find(" native=") - at);
    }
}

TEST(XnaAudioConvertFormat, ThePcmIdentityMatchesXnaByteForByte)
{
    ScratchDirectory scratch("pcm");
    const std::string mono = WriteWav(scratch.Path(), "mono8k.wav", 8000, 1, 16, 800);
    const std::string stereo = WriteWav(scratch.Path(), "stereo44k.wav", 44100, 2, 16, 4410);
    const std::string eightBit = WriteWav(scratch.Path(), "mono8bit.wav", 22050, 1, 8, 2205);

    AudioContent best(stereo, AudioFileType::Wav);
    best.ConvertFormat(ConversionFormat::Pcm, ConversionQuality::Best, "");
    EXPECT_EQ(Describe(best), Expected("convert/pcm_best"));

    AudioContent eight(eightBit, AudioFileType::Wav);
    eight.ConvertFormat(ConversionFormat::Pcm, ConversionQuality::Best, "");
    EXPECT_EQ(Describe(eight), Expected("convert/pcm_from_8bit"));

    AudioContent unchanged(mono, AudioFileType::Wav);
    unchanged.ConvertFormat(ConversionFormat::Pcm, ConversionQuality::Best, "");
    EXPECT_EQ("unchanged=" + Hex(unchanged.getDataProperty(), 16),
              Expected("convert/pcm_best_first_bytes"));
}

TEST(XnaAudioConvertFormat, TheResampledShapesMatchXna)
{
    ScratchDirectory scratch("resample");
    const std::string stereo = WriteWav(scratch.Path(), "stereo44k.wav", 44100, 2, 16, 4410);
    // The sample values a resampler produces are its own -- XNA's lives in a native helper -- so
    // what is compared here is the shape XNA answered: the rate, the depth, the channel count, the
    // byte rate, the data length, the loop and the duration.
    AudioContent low(stereo, AudioFileType::Wav);
    low.ConvertFormat(ConversionFormat::Pcm, ConversionQuality::Low, "");
    EXPECT_EQ(Describe(low).substr(0, Describe(low).find(" native=")),
              Expected("convert/pcm_low").substr(0, Expected("convert/pcm_low").find(" native=")));

    AudioContent medium(stereo, AudioFileType::Wav);
    medium.ConvertFormat(ConversionFormat::Pcm, ConversionQuality::Medium, "");
    EXPECT_EQ(Describe(medium).substr(0, Describe(medium).find(" native=")),
              Expected("convert/pcm_medium").substr(0, Expected("convert/pcm_medium").find(" native=")));
}

TEST(XnaAudioConvertFormat, AdpcmIsWrittenInXnasOwnBlockGeometry)
{
    ScratchDirectory scratch("adpcm");
    const std::string stereo = WriteWav(scratch.Path(), "stereo44k.wav", 44100, 2, 16, 4410);
    AudioContent audio(stereo, AudioFileType::Wav);
    audio.ConvertFormat(ConversionFormat::Adpcm, ConversionQuality::Best, "");
    const std::shared_ptr<AudioFormat> format = audio.getFormatProperty();
    // XNA's own ADPCM pass resamples to a rate of its encoder's choosing (43519 Hz for a 44100
    // source), which no in-house encoder reproduces; what is held to the measurement is the block
    // geometry, which is the part a decoder depends on: format 2, four bits, 140-byte blocks of
    // 128 frames, and a 32-byte extension carrying the seven standard coefficient pairs.
    const std::string expected = Expected("convert/adpcm_best");
    EXPECT_NE(expected.find("format=2"), std::string::npos);
    EXPECT_EQ(format->getFormatProperty(), 2);
    EXPECT_EQ(format->getBitsPerSampleProperty(), 4);
    EXPECT_EQ(format->getBlockAlignProperty(), 140);
    EXPECT_EQ(format->getChannelCountProperty(), 2);
    EXPECT_EQ(format->getSampleRateProperty(), 44100);
    EXPECT_EQ(format->getAverageBytesPerSecondProperty(), 44100 * 140 / 128);
    ASSERT_EQ(format->getNativeWaveFormatProperty().size(), 18u + 32u);
    EXPECT_EQ(Hex(format->getNativeWaveFormatProperty(), 18u + 32u).substr(32, 8), "20008000");
    EXPECT_EQ(audio.getDataProperty().size() % 140u, 0u);
    EXPECT_EQ(audio.getLoopLengthProperty(), 35 * 128);
}

TEST(XnaAudioConvertFormat, AdpcmRoundTripsThroughCnasOwnDecoder)
{
    ScratchDirectory scratch("roundtrip");
    // A sine, not the ramp the other cases use: that ramp is a wrapping sawtooth at full scale,
    // which is noise, and no ADPCM encoder carries noise.
    const std::string mono = WriteSineWav(scratch.Path(), "sine8k.wav", 8000, 800, 120.0);
    AudioContent source(mono, AudioFileType::Wav);
    const std::vector<SharpRuntime::bytecs> original = source.getDataProperty();
    source.ConvertFormat(ConversionFormat::Adpcm, ConversionQuality::Best, "");
    const std::shared_ptr<AudioFormat> format = source.getFormatProperty();
    const std::vector<std::uint8_t> wav = CNA::Internal::Audio::BuildWavFromWaveFormatEx(
        source.getDataProperty().data(), static_cast<std::uint32_t>(source.getDataProperty().size()),
        static_cast<std::uint16_t>(format->getFormatProperty()),
        static_cast<std::uint16_t>(format->getChannelCountProperty()),
        static_cast<std::uint32_t>(format->getSampleRateProperty()),
        static_cast<std::uint32_t>(format->getAverageBytesPerSecondProperty()),
        static_cast<std::uint16_t>(format->getBlockAlignProperty()),
        static_cast<std::uint16_t>(format->getBitsPerSampleProperty()),
        CNA::Internal::Audio::MsAdpcmFormatExtension(128), 0u);
    const CNA::Internal::Audio::DecodedWavPcm16 decoded =
        CNA::Internal::Audio::DecodeWavToPcm16(wav, "adpcm");
    ASSERT_EQ(decoded.channels, 1u);
    ASSERT_EQ(decoded.sampleRate, 8000u);
    ASSERT_GE(decoded.samples.size(), original.size());
    // The encoder is lossy, so the round trip is held to a bound rather than to equality: the
    // ramp this file carries comes back within a few hundred of every sample.
    double worst = 0.0;
    for (std::size_t i = 0; i + 1 < original.size(); i += 2)
    {
        const auto before = static_cast<std::int16_t>(static_cast<std::uint16_t>(original[i]) |
                                                      static_cast<std::uint16_t>(original[i + 1] << 8));
        const auto after = static_cast<std::int16_t>(static_cast<std::uint16_t>(decoded.samples[i]) |
                                                     static_cast<std::uint16_t>(decoded.samples[i + 1] << 8));
        worst = std::max(worst, std::abs(static_cast<double>(before) - after));
    }
    // Measured on this signal: the peak reconstruction error is 449 of 20000, a little over two
    // percent, which is what a four-bit encoder gives a tone at a sixty-sixth of the sample rate.
    EXPECT_LT(worst, 500.0) << "MS-ADPCM round trip drifted by " << worst;
}

TEST(XnaAudioConvertFormat, TheTwoPlatformEncodersAreRefusedByName)
{
    ScratchDirectory scratch("blocked");
    const std::string mono = WriteWav(scratch.Path(), "mono8k.wav", 8000, 1, 16, 800);
    AudioContent audio(mono, AudioFileType::Wav);
    // Neither could be measured either: XNA's Windows Media encoder never returns under the Wine
    // prefix the oracle runs in, and the XMA encoder ships only with the Xbox 360 tools.
    EXPECT_THROW(audio.ConvertFormat(ConversionFormat::WindowsMedia, ConversionQuality::Best, ""),
                 InvalidContentException);
    EXPECT_THROW(audio.ConvertFormat(ConversionFormat::Xma, ConversionQuality::Best, ""),
                 InvalidContentException);
    EXPECT_EQ(audio.getFormatProperty()->getFormatProperty(), 1);
}

