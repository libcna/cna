// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-262: the build-time XMA codec seam.
//
// XMA is the one format in this plan CNA cannot produce: Microsoft's own Xbox 360 codec, with no
// public specification sufficient to implement a conforming encoder, no licensable encoder, and
// none in FFmpeg, which decodes xma1 and xma2 and encodes neither. What can be built here is
// everything *around* the encoder, and one interface where a user attaches theirs -- and that is
// what these tests are. The encoder they attach is a stub written by the test: it proves the
// contract, the diagnostics, the fingerprint identity and the refusals, none of which needs a real
// encoder to be correct.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/XmaEncoderService.hpp"
#include <iterator>
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"

namespace Build = CNA::Content::Pipeline;
namespace Audio = Microsoft::Xna::Framework::Content::Pipeline::Audio;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;

namespace
{
    /** @brief A scratch directory that removes itself. */
    class Scratch
    {
    public:
        explicit Scratch(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp262_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~Scratch()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    /** @brief Restores whatever encoder was attached, so one test cannot leak into the next. */
    class AttachedEncoder
    {
    public:
        explicit AttachedEncoder(std::shared_ptr<const Build::XmaEncoderService> encoder)
        {
            Build::SetBuildXmaEncoder(std::move(encoder));
        }
        ~AttachedEncoder() { Build::SetBuildXmaEncoder(nullptr); }
        AttachedEncoder(const AttachedEncoder&) = delete;
        AttachedEncoder& operator=(const AttachedEncoder&) = delete;
    };

    void PutU16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    void PutU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void PutTag(std::vector<std::uint8_t>& bytes, const char* tag)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes.push_back(static_cast<std::uint8_t>(tag[index]));
        }
    }

    /** @brief Writes a small PCM16 WAV for AudioContent to read. */
    std::string WriteWav(const std::filesystem::path& directory, const std::string& name)
    {
        constexpr int rate = 8000;
        constexpr int frames = 400;
        std::vector<std::uint8_t> pcm;
        for (int index = 0; index < frames; ++index)
        {
            const auto sample = static_cast<std::int16_t>(index * 37);
            PutU16(pcm, static_cast<std::uint16_t>(sample));
        }

        std::vector<std::uint8_t> bytes;
        PutTag(bytes, "RIFF");
        PutU32(bytes, static_cast<std::uint32_t>(36u + pcm.size()));
        PutTag(bytes, "WAVE");
        PutTag(bytes, "fmt ");
        PutU32(bytes, 16u);
        PutU16(bytes, 1u);
        PutU16(bytes, 1u);
        PutU32(bytes, rate);
        PutU32(bytes, rate * 2u);
        PutU16(bytes, 2u);
        PutU16(bytes, 16u);
        PutTag(bytes, "data");
        PutU32(bytes, static_cast<std::uint32_t>(pcm.size()));
        bytes.insert(bytes.end(), pcm.begin(), pcm.end());

        const std::filesystem::path path = directory / name;
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return path.string();
    }

    /**
     * @brief Writes an executable stub that satisfies the documented encoder contract.
     *
     * It is not an XMA encoder and does not pretend to be one: it reads the RIFF WAVE it is handed
     * and writes a RIFF whose `fmt ` chunk is an XMA2WAVEFORMATEX and whose `data` chunk is a
     * quarter of the input, which is the shape of the thing. What is under test is the seam.
     */
    std::filesystem::path WriteStubEncoder(const std::filesystem::path& directory,
                                           const std::string& name, const std::string& body)
    {
        const std::filesystem::path path = directory / name;
        { std::ofstream(path) << body; }
        std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add);
        return path;
    }

    /** @brief The stub that behaves. */
    const char* kGoodStub = R"(#!/usr/bin/env python3
import struct, sys
source = open(sys.argv[1], 'rb').read()
at = source.index(b'data')
pcm = source[at + 8:]
payload = pcm[:len(pcm) // 4]
# An XMA2WAVEFORMATEX: the sixteen WAVEFORMATEX bytes, cbSize, then 34 more.
fmt = struct.pack('<HHIIHHH', 0x0166, 1, 8000, 4000, 2, 16, 34) + bytes(34)
body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + \
       b'data' + struct.pack('<I', len(payload)) + payload
open(sys.argv[2], 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
)";

    /** @brief The stub that writes something that is not a `.xma`. */
    const char* kWrongStub = R"(#!/usr/bin/env python3
import sys
open(sys.argv[2], 'wb').write(b'not a riff file at all')
)";

    /** @brief The stub that fails and says why. */
    const char* kFailingStub = R"(#!/usr/bin/env python3
import sys
sys.stderr.write('the sample rate 8000 is below what this encoder supports\n')
sys.exit(3)
)";
}

// With nothing attached, the reason is the exact sentence the matrix and the plan carry.
TEST(XmaEncoderService, WithNothingAttachedTheReasonIsTheExactSentence)
{
    Build::SetBuildXmaEncoder(nullptr);
    const std::shared_ptr<const Build::XmaEncoderService> encoder = Build::BuildXmaEncoder();
    ASSERT_NE(encoder, nullptr);
    EXPECT_FALSE(encoder->Available());
    EXPECT_EQ(encoder->UnavailableReason().rfind(Build::XmaEncoderUnavailableSentence, 0u), 0u)
        << encoder->UnavailableReason();
    // The sentence is followed by the audit, so a reader is told why rather than only that.
    EXPECT_NE(encoder->UnavailableReason().find("FFmpeg"), std::string::npos);
    EXPECT_TRUE(encoder->Identity().ToString().empty());

    // And an encode through it is a refusal carrying the same sentence.
    const Build::XmaEncodeResult result = encoder->Encode({});
    EXPECT_FALSE(result.succeeded);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_NE(result.diagnostics.front().find(Build::XmaEncoderUnavailableSentence),
              std::string::npos);
}

// XNA's own API for asking for XMA refuses with it too, rather than with a sentence of its own.
TEST(XmaEncoderService, ConvertFormatRefusesWithTheSameSentence)
{
    Build::SetBuildXmaEncoder(nullptr);
    const Scratch scratch("refuse");
    Audio::AudioContent audio(WriteWav(scratch.Path(), "tone.wav"), Audio::AudioFileType::Wav);
    try
    {
        audio.ConvertFormat(Audio::ConversionFormat::Xma, Audio::ConversionQuality::Best, "");
        ADD_FAILURE() << "XMA was produced with no encoder attached";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find(Build::XmaEncoderUnavailableSentence),
                  std::string::npos)
            << error.getMessageProperty();
    }
}

// Naming an encoder that is not there is its own refusal, and not the unavailable sentence: the
// two are different mistakes and a user has to be able to tell them apart.
TEST(XmaEncoderService, NamingAnEncoderThatIsNotThereSaysSoByPath)
{
    Build::ExternalXmaEncoderOptions options;
    options.executable = "/nonexistent/xma2encode.exe";
    const std::shared_ptr<const Build::XmaEncoderService> encoder =
        Build::MakeExternalXmaEncoder(options);
    EXPECT_FALSE(encoder->Available());
    EXPECT_NE(encoder->UnavailableReason().find("/nonexistent/xma2encode.exe"), std::string::npos);
    EXPECT_EQ(encoder->UnavailableReason().find(Build::XmaEncoderUnavailableSentence),
              std::string::npos)
        << "a wrong path is not the same finding as no encoder existing";
}

// An attached encoder is driven by the documented contract, and what it wrote becomes the content.
TEST(XmaEncoderService, AnAttachedEncoderIsDrivenByTheDocumentedContract)
{
    const Scratch scratch("attached");
    Build::ExternalXmaEncoderOptions options;
    options.executable = WriteStubEncoder(scratch.Path(), "stub-encoder", kGoodStub);
    const std::shared_ptr<const Build::XmaEncoderService> encoder =
        Build::MakeExternalXmaEncoder(options);
    ASSERT_TRUE(encoder->Available()) << encoder->UnavailableReason();
    EXPECT_FALSE(encoder->Identity().ToString().empty()) << "an attached encoder must fingerprint";
    const AttachedEncoder attached(encoder);

    Audio::AudioContent audio(WriteWav(scratch.Path(), "tone.wav"), Audio::AudioFileType::Wav);
    const auto pcmBytes = static_cast<std::size_t>(audio.getDataProperty().size());
    ASSERT_GT(pcmBytes, 0u);

    audio.ConvertFormat(Audio::ConversionFormat::Xma, Audio::ConversionQuality::Best, "");

    // The format the encoder declared is the content's format now, base fields and extension alike.
    ASSERT_NE(audio.getFormatProperty(), nullptr);
    EXPECT_EQ(audio.getFormatProperty()->getFormatProperty(), 0x0166);
    EXPECT_EQ(audio.getFormatProperty()->getChannelCountProperty(), 1);
    EXPECT_EQ(audio.getFormatProperty()->getSampleRateProperty(), 8000);
    EXPECT_EQ(audio.getFormatProperty()->getNativeWaveFormatProperty().size() - 18u, 34u)
        << "the XMA2 extension travels with the format";
    EXPECT_EQ(audio.getDataProperty().size(), pcmBytes / 4u);
}

// An encoder that writes something else is refused, and the refusal says what was wrong with it.
TEST(XmaEncoderService, AnEncoderThatWritesSomethingElseIsRefused)
{
    const Scratch scratch("wrong");
    Build::ExternalXmaEncoderOptions options;
    options.executable = WriteStubEncoder(scratch.Path(), "stub-encoder", kWrongStub);
    const AttachedEncoder attached(Build::MakeExternalXmaEncoder(options));

    Audio::AudioContent audio(WriteWav(scratch.Path(), "tone.wav"), Audio::AudioFileType::Wav);
    try
    {
        audio.ConvertFormat(Audio::ConversionFormat::Xma, Audio::ConversionQuality::Best, "");
        ADD_FAILURE() << "a file that is not a .xma was accepted as one";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("RIFF"), std::string::npos)
            << error.getMessageProperty();
    }
}

// And an encoder that fails is reported in its own words rather than summarized away.
TEST(XmaEncoderService, AnEncoderThatFailsIsReportedInItsOwnWords)
{
    const Scratch scratch("failing");
    Build::ExternalXmaEncoderOptions options;
    options.executable = WriteStubEncoder(scratch.Path(), "stub-encoder", kFailingStub);
    const AttachedEncoder attached(Build::MakeExternalXmaEncoder(options));

    Audio::AudioContent audio(WriteWav(scratch.Path(), "tone.wav"), Audio::AudioFileType::Wav);
    try
    {
        audio.ConvertFormat(Audio::ConversionFormat::Xma, Audio::ConversionQuality::Best, "");
        ADD_FAILURE() << "an encoder that exited non-zero was treated as a success";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("below what this encoder supports"),
                  std::string::npos)
            << error.getMessageProperty();
        EXPECT_NE(error.getMessageProperty().find("status 3"), std::string::npos)
            << error.getMessageProperty();
    }
}

// The argument template is what adapts CNA to an encoder's own command line.
TEST(XmaEncoderService, TheArgumentTemplateIsSubstitutedAndTheQualityReachesIt)
{
    const Scratch scratch("template");
    // This stub only works if it is given the arguments in the order the template names.
    const char* stub = R"(#!/usr/bin/env python3
import struct, sys
assert sys.argv[1] == '--quality', sys.argv
quality = sys.argv[2]
source = open(sys.argv[4], 'rb').read()
at = source.index(b'data')
payload = source[at + 8:][:16]
fmt = struct.pack('<HHIIHHH', 0x0166, 1, 8000, 4000, 2, 16, 34) + bytes(34)
body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + \
       b'data' + struct.pack('<I', len(payload)) + payload
open(sys.argv[6], 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
sys.stderr.write('quality=' + quality + '\n')
)";
    Build::ExternalXmaEncoderOptions options;
    options.executable = WriteStubEncoder(scratch.Path(), "stub-encoder", stub);
    options.arguments = {"--quality", "{quality}", "--in", "{input}", "--out", "{output}"};
    const std::shared_ptr<const Build::XmaEncoderService> encoder =
        Build::MakeExternalXmaEncoder(options);
    ASSERT_TRUE(encoder->Available()) << encoder->UnavailableReason();

    Build::XmaEncodeRequest request;
    request.pcm.assign(64u, 0u);
    request.quality = Build::XmaEncodeQuality::Medium;
    const Build::XmaEncodeResult result = encoder->Encode(request);
    ASSERT_TRUE(result.succeeded) << (result.diagnostics.empty() ? "" : result.diagnostics.front());
    EXPECT_EQ(result.data.size(), 16u);
    bool sawQuality = false;
    for (const std::string& line : result.diagnostics)
    {
        sawQuality = sawQuality || line.find("quality=medium") != std::string::npos;
    }
    EXPECT_TRUE(sawQuality) << "the quality did not reach the encoder's command line";
}

// Two encoders are two fingerprints: attaching or changing one has to rebuild rather than reuse.
TEST(XmaEncoderService, TwoEncodersHaveTwoIdentities)
{
    const Scratch scratch("identity");
    Build::ExternalXmaEncoderOptions first;
    first.executable = WriteStubEncoder(scratch.Path(), "encoder-a", kGoodStub);
    Build::ExternalXmaEncoderOptions second;
    second.executable = WriteStubEncoder(scratch.Path(), "encoder-b", kGoodStub);

    const std::string one = Build::MakeExternalXmaEncoder(first)->Identity().ToString();
    const std::string other = Build::MakeExternalXmaEncoder(second)->Identity().ToString();
    EXPECT_FALSE(one.empty());
    EXPECT_NE(one, other);
}


namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }
}

// What XMA actually is, read out of the file genuine XNA wrote. This is the measurement that says
// what an attached encoder has to produce and what CNA has to do with it, and it is here rather
// than only in the documentation so that a change to either is a failing test.
TEST(XmaEncoderService, TheGenuineXboxSoundEffectDecomposesAsABigEndianXma2WaveFormatEx)
{
    const std::filesystem::path genuine =
        Locate("tests/reference/xna40/differential/audio_wav_soundeffect_xbox360.xnb");
    ASSERT_TRUE(std::filesystem::is_regular_file(genuine)) << genuine.string();
    std::ifstream stream(genuine, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),
                                          std::istreambuf_iterator<char>()};
    ASSERT_GT(bytes.size(), 128u);

    // The Xbox 360 is big-endian and XNA byte-swaps the whole format block for it, which is why
    // the tag reads 0x0166 only when it is read that way: the little-endian reading is 0x6601.
    std::size_t at = 0u;
    for (; at + 2u < bytes.size(); ++at)
    {
        if (bytes[at] == 0x01u && bytes[at + 1u] == 0x66u) { break; }
    }
    ASSERT_LT(at + 52u, bytes.size()) << "no big-endian XMA2 format tag in the genuine file";

    const auto be = [&bytes, at](const std::size_t offset, const std::size_t width) {
        std::uint32_t value = 0u;
        for (std::size_t index = 0u; index < width; ++index)
        {
            value = (value << 8) | bytes[at + offset + index];
        }
        return value;
    };

    // WAVEFORMATEX, big-endian. The sample rate is not the source's 44100: it is 44032, which is
    // 344 x 128, the subframe an XMA stream is built out of.
    EXPECT_EQ(be(0u, 2u), 0x0166u);
    EXPECT_EQ(be(2u, 2u), 1u);
    EXPECT_EQ(be(4u, 4u), 44032u);
    EXPECT_EQ(be(8u, 4u), 12200u);
    EXPECT_EQ(be(12u, 2u), 2u);
    EXPECT_EQ(be(14u, 2u), 16u);
    EXPECT_EQ(be(16u, 2u), 34u) << "cbSize: an XMA2WAVEFORMATEX is 34 bytes past the base";

    // XMA2WAVEFORMATEX, the 34 bytes after cbSize, in declaration order.
    EXPECT_EQ(be(18u, 2u), 1u) << "NumStreams";
    EXPECT_EQ(be(20u, 4u), 1u) << "ChannelMask: SPEAKER_FRONT_LEFT";
    EXPECT_EQ(be(24u, 4u), 22528u) << "SamplesEncoded, a whole number of 128-sample subframes";
    EXPECT_EQ(be(28u, 4u), 32768u) << "BytesPerBlock";
    EXPECT_EQ(be(32u, 4u), 0u) << "PlayBegin";
    EXPECT_EQ(be(36u, 4u), 22050u) << "PlayLength: the source's own frame count, not quantized";
    EXPECT_EQ(be(40u, 4u), 384u) << "LoopBegin: 3 x 128";
    EXPECT_EQ(be(44u, 4u), 22016u) << "LoopLength: 172 x 128";
    EXPECT_EQ(be(48u, 1u), 0u) << "LoopCount";
    EXPECT_EQ(be(49u, 1u), 4u) << "EncoderVersion";
    EXPECT_EQ(be(50u, 2u), 1u) << "BlockCount";

    // And the same source built for Windows: PCM, the rate untouched, the loop exact. The two
    // together are what "XMA quantizes and PCM does not" means.
    const std::filesystem::path windows =
        Locate("tests/reference/xna40/differential/audio_wav_soundeffect.xnb");
    ASSERT_TRUE(std::filesystem::is_regular_file(windows));
    std::ifstream pcmStream(windows, std::ios::binary);
    const std::vector<std::uint8_t> pcm{std::istreambuf_iterator<char>(pcmStream),
                                        std::istreambuf_iterator<char>()};
    bool sawPcmTag = false;
    for (std::size_t index = 0u; index + 16u < pcm.size(); ++index)
    {
        // WAVEFORMATEX little-endian: tag 1, one channel, 44100 Hz.
        if (pcm[index] == 0x01u && pcm[index + 1u] == 0x00u && pcm[index + 2u] == 0x01u &&
            pcm[index + 3u] == 0x00u && pcm[index + 4u] == 0x44u && pcm[index + 5u] == 0xACu)
        {
            sawPcmTag = true;
            break;
        }
    }
    EXPECT_TRUE(sawPcmTag) << "the Windows build is little-endian PCM at the source's own rate";
}
