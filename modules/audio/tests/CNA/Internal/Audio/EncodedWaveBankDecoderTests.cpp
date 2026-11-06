// SPDX-License-Identifier: MS-PL
// Independent SDK corpus and explicit unqualified-codec refusal contracts.
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace CNA::Internal::Audio;

#if defined(CNA_AUDIO_WAVEBANK_FFMPEG_AVAILABLE)
struct EncodedCase
{
    const char* stem;
    EncodedWaveBankCodec codec;
    std::uint32_t rate;
    std::uint8_t channels;
};

// Phase 3 qualification: WMAv2 only. Genuine Pro's authored and decoded lengths
// differ; the separate refusal test must hold even for a shorter duration.
constexpr EncodedCase cases[] = {
    {"wmav2",EncodedWaveBankCodec::Wma2,44100,2}
};

XwbData BoundaryBank(XwbFormat tag=XwbFormat::WMA)
{
    XwbData bank; bank.version=46; bank.headerVersion=44; bank.bankName="Gap05Boundary";
    XwbEntry entry{}; entry.format=tag; entry.channels=2; entry.sampleRate=44100;
    entry.miniFormat=static_cast<std::uint32_t>(tag)|(2u<<2)|(44100u<<5)|(3u<<23);
    entry.durationSamples=8192; entry.dataLength=1; bank.entries.push_back(entry);
    return bank;
}

template<class Function> void RefusesWith(Function&& function,const char* message)
{
    try { function(); FAIL()<<"accepted invalid/unqualified encoded entry"; }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find(message),std::string::npos) << error.what();
    }
}

TEST(EncodedWaveBankDecoderBoundary, OversizedDeclaredOutputRefusesBeforeOpeningTheCodec)
{
    auto bank=BoundaryBank();
    // Literal byte budget: (256MiB / (2ch*2bytes)) +1 frames would exceed the
    // approved limit. A one-byte negative control never allocates that output.
    bank.entries[0].durationSamples=EncodedWaveBankByteLimit/4+1;
    const std::array<std::uint8_t,1> bytes{0};
    RefusesWith([&]{(void)OpenEncodedWaveBankDecoder(bank,0,bytes);},"authored duration exceeds");
}

TEST(EncodedWaveBankDecoderBoundary, InvalidPackedIndicesRefuseBeforePacketDecoding)
{
    const std::array<std::uint8_t,1> bytes{0};
    for (std::uint32_t packed : {31u,224u})
    {
        auto bank=BoundaryBank();
        bank.entries[0].miniFormat=(bank.entries[0].miniFormat&~(255u<<23))|(packed<<23);
        // FACT.h arrays have17 and7 elements: low5 index31 / high3 index7 are invalid.
        RefusesWith([&]{(void)OpenEncodedWaveBankDecoder(bank,0,bytes);},"packed block alignment or byte-rate index");
    }
}

TEST(EncodedWaveBankDecoderBoundary, BothXmaSelectorsRemainExplicitlyUnqualified)
{
    const std::array<std::uint8_t,1> bytes{0};
    for (auto version : {43u,46u}) for (bool high : {false,true})
    {
        auto bank=BoundaryBank(XwbFormat::XMA); bank.version=version;
        if (high) bank.entries[0].miniFormat|=1u<<31;
        // Owner decision: no real XMA fixture exists. Verify the qualification
        // gate itself, without mislabelling this dummy payload as encoded audio.
        RefusesWith([&]{(void)OpenEncodedWaveBankDecoder(bank,0,bytes);},"XMA/XMA2 decoding is unqualified");
    }
}

std::vector<std::uint8_t> ReadRequired(const std::filesystem::path& path)
{
    std::ifstream source(path,std::ios::binary);
    if (!source) throw std::runtime_error("Required licensed GAP-05 fixture missing: "+path.string());
    return {std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>()};
}

struct Fixture
{
    XwbData bank;
    std::vector<std::uint8_t> encoded;
    std::vector<std::int16_t> golden;
    explicit Fixture(const EncodedCase& test)
    {
        const auto root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT);
        bank=ParseXwb(ReadRequired(root/(std::string(test.stem)+".xwb")));
        if (bank.entries.size()!=1) throw std::runtime_error("Fixture must have one encoded entry");
        const auto& e=bank.entries[0];
        const auto begin=static_cast<std::uint64_t>(e.dataOffset);
        if (begin+e.dataLength>bank.fileData.size()) throw std::runtime_error("Truncated fixture entry");
        encoded.assign(bank.fileData.begin()+e.dataOffset,bank.fileData.begin()+e.dataOffset+e.dataLength);
        auto raw=ReadRequired(root/(std::string(test.stem)+".s16le"));
        if (raw.size()!=8192u*test.channels*2u) throw std::runtime_error("Golden must contain exactly 8192 frames");
        golden.reserve(raw.size()/2);
        for (std::size_t i=0; i<raw.size(); i+=2)
        {
            const auto word=std::uint16_t(raw[i])|(std::uint16_t(raw[i+1])<<8);
            golden.push_back(static_cast<std::int16_t>(word>=32768 ? int(word)-65536 : int(word)));
        }
    }
};

void ExpectPcm(std::span<const std::int16_t> actual,std::span<const std::int16_t> reference)
{
    ASSERT_EQ(actual.size(),reference.size());
    for (std::size_t i=0; i<actual.size(); ++i)
        // Independent system ffmpeg CLI golden. Two integer LSBs permit different
        // libavcodec SIMD/float rounding; no shift, scaling or alignment fitting.
        ASSERT_LE(std::abs(int(actual[i])-int(reference[i])),2) << "sample " << i;
}

class EncodedWaveBankDecoderTest : public ::testing::TestWithParam<EncodedCase> {};

TEST(EncodedWaveBankDecoderBoundary, GenuineWmaProPreservesAuthoredDurationAndRefusesEvenATrimmedTimeline)
{
    const auto root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT);
    auto bank=ParseXwb(ReadRequired(root/"wmapro.native.xwb"));
    ASSERT_EQ(bank.entries.size(),1u);
    const auto& entry=bank.entries[0];
    // SDK xact3wb.h:459-465 duration is native frames; PROVENANCE.json records
    // 3 packets of 5462 bytes, dpds.back()/12=37713, CLI output=35665 at48000Hz.
    // Never rewrite this genuine authored duration to the source's44100Hz count.
    EXPECT_EQ(entry.miniFormat,0xb797701bu);
    EXPECT_EQ(entry.sampleRate,48000u); EXPECT_EQ(entry.channels,6);
    EXPECT_EQ(entry.durationSamples,37713u); EXPECT_EQ(entry.dataLength,16386u);
    EXPECT_EQ(entry.seekTable,(std::vector<std::uint32_t>{196608,393216,452556}));
    ASSERT_LE(static_cast<std::uint64_t>(entry.dataOffset)+entry.dataLength,bank.fileData.size());
    const auto encoded=std::span(bank.fileData).subspan(entry.dataOffset,entry.dataLength);
    for (auto duration : {37713u,8192u,0u})
    {
        SCOPED_TRACE(duration);
        bank.entries[0].durationSamples=duration;
        RefusesWith([&]{(void)OpenEncodedWaveBankDecoder(bank,0,encoded);},
                    "WMA Pro decoding is unqualified");
    }
}

TEST_P(EncodedWaveBankDecoderTest, ExactCodecFrameCountRateChannelsAndReferencePcm)
{
    const auto test=GetParam(); Fixture f(test);
    // Golden provenance + exact fixture recipe: encoded-wavebank/README.md.
    EXPECT_EQ(f.bank.entries[0].durationSamples,8192u);
    auto decoder=OpenEncodedWaveBankDecoder(f.bank,0,f.encoded);
    ASSERT_TRUE(decoder); EXPECT_EQ(decoder->Codec(),test.codec);
    EXPECT_EQ(decoder->SampleRate(),test.rate); EXPECT_EQ(decoder->Channels(),test.channels);
    EXPECT_EQ(decoder->FrameCount(),8192u);
    std::vector<std::int16_t> actual(8192u*test.channels,std::numeric_limits<std::int16_t>::min());
    EXPECT_EQ(decoder->ReadFrames(actual),8192u);
    ExpectPcm(actual,f.golden);
    // Output must have signal in every authored channel; zero-filled success is not a decoder.
    for (unsigned c=0; c<test.channels; ++c)
    {
        bool signal=false;
        for (unsigned frame=0; frame<8192; ++frame) signal|=std::abs(int(actual[frame*test.channels+c]))>64;
        EXPECT_TRUE(signal) << "channel " << c;
    }
}

TEST_P(EncodedWaveBankDecoderTest, ArbitraryReadSizesAndSeeksMatchIndependentFrameSlices)
{
    const auto test=GetParam(); Fixture f(test);
    auto decoder=OpenEncodedWaveBankDecoder(f.bank,0,f.encoded); ASSERT_TRUE(decoder);
    // Frame offsets, not bytes or interleaved scalar counts. Cross packet/MDCT boundaries.
    for (std::uint32_t frame : {0u,1u,511u,512u,2047u,2048u,4095u,4096u,8125u})
    {
        SCOPED_TRACE(frame);
        decoder->SeekFrame(frame);
        std::vector<std::int16_t> actual(67u*test.channels);
        ASSERT_EQ(decoder->ReadFrames(actual),67u);
        ExpectPcm(actual,std::span(f.golden).subspan(frame*test.channels,actual.size()));
    }
    // Successive small reads must not restart the compressed packet or decoder history.
    decoder->SeekFrame(0);
    for (std::uint32_t frame=0; frame<1024; frame+=16)
    {
        std::vector<std::int16_t> actual(16u*test.channels);
        ASSERT_EQ(decoder->ReadFrames(actual),16u);
        ExpectPcm(actual,std::span(f.golden).subspan(frame*test.channels,actual.size()));
    }
}

TEST_P(EncodedWaveBankDecoderTest, EofAndInvalidSeekHaveExactCountsAndDoNotWritePastOutput)
{
    const auto test=GetParam(); Fixture f(test);
    auto decoder=OpenEncodedWaveBankDecoder(f.bank,0,f.encoded); ASSERT_TRUE(decoder);
    // Internal adapter contract: only returned frames may be written; EOF is frame 8192.
    constexpr std::int16_t sentinel=0x2345;
    std::vector<std::int16_t> actual(64u*test.channels,sentinel);
    decoder->SeekFrame(8189);
    ASSERT_EQ(decoder->ReadFrames(actual),3u);
    ExpectPcm(std::span(actual).first(3u*test.channels),std::span(f.golden).subspan(8189u*test.channels));
    for (std::size_t i=3u*test.channels; i<actual.size(); ++i) EXPECT_EQ(actual[i],sentinel);
    std::fill(actual.begin(),actual.end(),sentinel);
    EXPECT_EQ(decoder->ReadFrames(actual),0u);
    decoder->SeekFrame(8192); EXPECT_EQ(decoder->ReadFrames(actual),0u);
    for (auto value : actual) EXPECT_EQ(value,sentinel);
    decoder->SeekFrame(123);
    EXPECT_THROW(decoder->SeekFrame(8193),std::out_of_range);
    EXPECT_THROW(decoder->SeekFrame(std::numeric_limits<std::uint64_t>::max()),std::out_of_range);
    EXPECT_THROW(decoder->ReadFrames(std::span(actual).first(test.channels-1)),std::invalid_argument);
    ASSERT_EQ(decoder->ReadFrames(actual),64u);
    ExpectPcm(actual,std::span(f.golden).subspan(123u*test.channels,actual.size()));
}

TEST_P(EncodedWaveBankDecoderTest, TruncatedPayloadRefusesAndFreshDecoderRecovers)
{
    const auto test=GetParam(); Fixture f(test);
    // Declared PlayRegion length cannot be satisfied by these exact entry spans.
    ASSERT_GT(f.encoded.size(),1u);
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(f.bank,0,std::span(f.encoded).first(f.encoded.size()-1)),std::runtime_error);
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(f.bank,0,std::span(f.encoded).first(f.encoded.size()/2)),std::runtime_error);
    for (int i=0; i<64; ++i)
    {
        auto decoder=OpenEncodedWaveBankDecoder(f.bank,0,f.encoded); ASSERT_TRUE(decoder);
        std::vector<std::int16_t> frame(test.channels);
        ASSERT_EQ(decoder->ReadFrames(frame),1u);
        ExpectPcm(frame,std::span(f.golden).first(test.channels));
        decoder.reset(); decoder.reset(); // empty owners remain safe; run under available LSan later
    }
}

TEST_P(EncodedWaveBankDecoderTest, InvalidMetadataAndLoopOverflowRefuse)
{
    const auto test=GetParam(); Fixture f(test);
    auto tooLong=f.bank;
    // The unmodified SDK bank and independent CLI golden both contain32768
    // frames. An extra declared frame is a refusal, never a padded/shortened success.
    tooLong.entries[0].durationSamples=32769;
    RefusesWith([&]{(void)OpenEncodedWaveBankDecoder(tooLong,0,f.encoded);},
                "decoder ended before the declared playable frames");
    auto bad=f.bank; bad.entries[0].channels=0;
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(bad,0,f.encoded),std::runtime_error);
    bad=f.bank; bad.entries[0].sampleRate=0;
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(bad,0,f.encoded),std::runtime_error);
    bad=f.bank; bad.entries[0].loopStartSample=8191; bad.entries[0].loopTotalSamples=2;
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(bad,0,f.encoded),std::runtime_error);
    bad=f.bank; bad.entries[0].loopStartSample=0xfffffff0u; bad.entries[0].loopTotalSamples=32;
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(bad,0,f.encoded),std::runtime_error);
    if (test.codec==EncodedWaveBankCodec::Wma2 || test.codec==EncodedWaveBankCodec::WmaPro)
    {
        bad=f.bank; bad.entries[0].miniFormat|=255u<<23; // index31 and index7 are outside tables
        EXPECT_THROW((void)OpenEncodedWaveBankDecoder(bad,0,f.encoded),std::runtime_error);
        bad=f.bank;
        ASSERT_GE(bad.entries[0].seekTable.size(),2u);
        // Preserve packet count and whole-frame byte units; isolate monotonicity.
        bad.entries[0].seekTable.front()=bad.entries[0].seekTable[1]+test.channels*2;
        RefusesWith([&]{(void)OpenEncodedWaveBankDecoder(bad,0,f.encoded);},"cumulative values decrease");
    }
}

TEST_P(EncodedWaveBankDecoderTest, InvalidPacketCorpusRefusesRatherThanReturningSilence)
{
    const auto test=GetParam(); Fixture f(test);
    const auto root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT);
    // Provenance records CLI -xerror refusal after corrupting only the final
    // packet. Earlier packets decode past the8192-frame trim: ignoring unread
    // tail errors must not turn this same-length payload into silent success.
    const auto invalid=ReadRequired(root/(std::string(test.stem)+".invalid.bin"));
    ASSERT_EQ(invalid.size(),f.encoded.size());
    EXPECT_THROW((void)OpenEncodedWaveBankDecoder(f.bank,0,invalid),std::runtime_error);
    auto decoder=OpenEncodedWaveBankDecoder(f.bank,0,f.encoded); ASSERT_TRUE(decoder);
    std::vector<std::int16_t> actual(67u*test.channels);
    ASSERT_EQ(decoder->ReadFrames(actual),67u);
    ExpectPcm(actual,std::span(f.golden).first(actual.size()));
}

TEST_P(EncodedWaveBankDecoderTest, GenuineNativeBankKeepsAllAuthoredFramesAndTheTail)
{
    const auto test=GetParam();
    const auto root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT);
    const auto bank=ParseXwb(ReadRequired(root/(std::string(test.stem)+".native.xwb")));
    ASSERT_EQ(bank.entries.size(),1u);
    const auto& entry=bank.entries[0];
    // The CC0 generator authors exactly32768 PCM frames. This is the unmodified
    // XactBld3 bank, separate from the8192-frame loop/seek test derivative.
    ASSERT_EQ(entry.durationSamples,32768u);
    ASSERT_LE(static_cast<std::uint64_t>(entry.dataOffset)+entry.dataLength,bank.fileData.size());
    const auto golden=ReadRequired(root/(std::string(test.stem)+".native.s16le"));
    ASSERT_EQ(golden.size(),32768u*test.channels*2u);
    auto decoder=OpenEncodedWaveBankDecoder(bank,0,
        std::span(bank.fileData).subspan(entry.dataOffset,entry.dataLength));
    ASSERT_TRUE(decoder); EXPECT_EQ(decoder->FrameCount(),32768u);
    std::vector<std::int16_t> actual(32768u*test.channels);
    ASSERT_EQ(decoder->ReadFrames(actual),32768u);
    for (std::size_t i=0; i<actual.size(); ++i)
    {
        const unsigned word=unsigned(golden[i*2])|(unsigned(golden[i*2+1])<<8);
        const int expected=word<32768 ? static_cast<int>(word) : static_cast<int>(word)-65536;
        ASSERT_LE(std::abs(int(actual[i])-expected),2) << "native sample " << i;
    }
}

INSTANTIATE_TEST_SUITE_P(Wmav2,EncodedWaveBankDecoderTest,::testing::ValuesIn(cases),
    [](const ::testing::TestParamInfo<EncodedCase>& info) { return info.param.stem; });

#else
TEST(EncodedWaveBankDecoderUnavailable, BothFormatsRefuseByName)
{
    // Owner's explicit refusal contract. No encoded positive fixture is claimed here.
    for (auto format : {XwbFormat::WMA,XwbFormat::XMA}) for (bool high : {false,true})
    {
        XwbData bank; bank.version=46; bank.headerVersion=44; bank.bankName="Gap05Unavailable";
        XwbEntry e{}; e.format=format; e.channels=2; e.sampleRate=44100;
        e.miniFormat=static_cast<std::uint32_t>(format)|(2u<<2)|(44100u<<5)|(3u<<23);
        if (high) e.miniFormat|=1u<<31;
        e.dataLength=1; e.durationSamples=8192; bank.entries.push_back(e);
        const std::array<std::uint8_t,1> bytes{0};
        try { auto decoder=OpenEncodedWaveBankDecoder(bank,0,bytes); FAIL()<<"accepted unavailable codec"; }
        catch (const std::runtime_error& error)
        {
            const std::string message=error.what();
            EXPECT_NE(message.find("FFmpeg"),std::string::npos) << message;
            const char* name=format==XwbFormat::WMA ? (high ? "WMA Pro" : "WMA/WMAv2") : "XMA/XMA2";
            EXPECT_NE(message.find(name),std::string::npos) << message;
        }
    }
}
#endif
} // namespace
