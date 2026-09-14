// SPDX-License-Identifier: MS-PL
//
// plans/plan_native_platforms_integration.md NPI-0009: DecodeWavToPcm16 no longer depends on
// SDL (see modules/audio/src/Internal/WavDecoder.cpp) -- these tests exercise that CNA-owned
// implementation directly and are run with CNA_ENABLE_SDL=OFF so they cannot pass merely because
// SDL happens to still be linked into the binary.

#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <numbers>
#include <vector>

#include "CNA/Internal/Audio/ImaAdpcmDecoder.hpp"
#include "CNA/Internal/Audio/MsAdpcmDecoder.hpp"
#include "CNA/Internal/Audio/MsAdpcmEncoder.hpp"
#include "CNA/Internal/Audio/WavDecoder.hpp"
#include "CNA/Internal/Audio/WavWrapper.hpp"

using CNA::Internal::Audio::AppendSmplChunkIfLooped;
using CNA::Internal::Audio::BuildWavFromWaveFormatEx;
using CNA::Internal::Audio::DecodedWavPcm16;
using CNA::Internal::Audio::DecodeWavToPcm16;

namespace
{
    void AppendU16(std::vector<std::uint8_t>& bytes, const std::uint16_t v)
    {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t v)
    {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
        bytes.push_back(static_cast<std::uint8_t>(v >> 16));
        bytes.push_back(static_cast<std::uint8_t>(v >> 24));
    }
    void AppendTag(std::vector<std::uint8_t>& bytes, const char* tag)
    {
        bytes.insert(bytes.end(), tag, tag + 4);
    }

    std::int16_t ReadSample(const std::vector<std::uint8_t>& samples, std::size_t index)
    {
        std::int16_t v = 0;
        std::memcpy(&v, samples.data() + index * 2u, 2u);
        return v;
    }

    // A minimal, hand-built RIFF/WAVE file: exactly RIFF/WAVE/fmt /data, no ancillary chunks,
    // no extension. `bitsPerSample` and `formatTag` let one builder cover every PCM/float case.
    std::vector<std::uint8_t> BuildPlainWav(
        const std::uint16_t formatTag, const std::uint16_t channels, const std::uint32_t sampleRate,
        const std::uint16_t bitsPerSample, const std::vector<std::uint8_t>& data)
    {
        const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8u));
        const std::uint32_t avgBytesPerSec = sampleRate * blockAlign;
        std::vector<std::uint8_t> wav;
        AppendTag(wav, "RIFF");
        AppendU32(wav, 0u); // patched below
        AppendTag(wav, "WAVE");
        AppendTag(wav, "fmt ");
        AppendU32(wav, 16u);
        AppendU16(wav, formatTag);
        AppendU16(wav, channels);
        AppendU32(wav, sampleRate);
        AppendU32(wav, avgBytesPerSec);
        AppendU16(wav, blockAlign);
        AppendU16(wav, bitsPerSample);
        AppendTag(wav, "data");
        AppendU32(wav, static_cast<std::uint32_t>(data.size()));
        wav.insert(wav.end(), data.begin(), data.end());
        if (data.size() % 2u != 0u)
        {
            wav.push_back(0u); // odd-size chunk padding
        }
        const std::uint32_t riffSize = static_cast<std::uint32_t>(wav.size() - 8u);
        std::memcpy(wav.data() + 4u, &riffSize, sizeof(riffSize));
        return wav;
    }
}

// --- valid minimal PCM, mono/stereo, multiple bit depths and sample rates -----------------------

TEST(WavDecoderTest, Pcm16MonoRoundTripsExactly)
{
    const std::vector<std::int16_t> source = {0, 32767, -32768, 12345, -12345};
    std::vector<std::uint8_t> data;
    for (const std::int16_t s : source)
    {
        data.push_back(static_cast<std::uint8_t>(s));
        data.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(s) >> 8));
    }
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 1, 44100, 16, data);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.channels, 1u);
    ASSERT_EQ(decoded.sampleRate, 44100u);
    ASSERT_EQ(decoded.frameCount, source.size());
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        EXPECT_EQ(ReadSample(decoded.samples, i), source[i]) << "frame " << i;
    }
}

TEST(WavDecoderTest, Pcm16StereoInterleavesCorrectly)
{
    // Frame 0: L=100, R=-100. Frame 1: L=200, R=-200.
    std::vector<std::uint8_t> data;
    for (const std::int16_t s : {100, -100, 200, -200})
    {
        data.push_back(static_cast<std::uint8_t>(s));
        data.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(s) >> 8));
    }
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 2, 22050, 16, data);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.channels, 2u);
    ASSERT_EQ(decoded.sampleRate, 22050u);
    ASSERT_EQ(decoded.frameCount, 2u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), 100);
    EXPECT_EQ(ReadSample(decoded.samples, 1), -100);
    EXPECT_EQ(ReadSample(decoded.samples, 2), 200);
    EXPECT_EQ(ReadSample(decoded.samples, 3), -200);
}

TEST(WavDecoderTest, Pcm8UnsignedCentersAtZero)
{
    // 8-bit PCM is offset-binary: 0 -> -32768, 128 -> 0, 255 -> max.
    const std::vector<std::uint8_t> data = {0u, 128u, 255u};
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 1, 8000, 8, data);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 3u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), -32768);
    EXPECT_EQ(ReadSample(decoded.samples, 1), 0);
    EXPECT_GT(ReadSample(decoded.samples, 2), 32000);
}

TEST(WavDecoderTest, Pcm24SignExtendsNegativeValues)
{
    std::vector<std::uint8_t> data;
    // Most-negative 24-bit value, little-endian: 00 00 80 -> integer -8388608 (full-scale negative).
    data.insert(data.end(), {0x00u, 0x00u, 0x80u});
    // Max positive 24-bit: 0x7FFFFF.
    data.insert(data.end(), {0xFFu, 0xFFu, 0x7Fu});
    // Integer -1 (0xFFFFFF): sign-extension must produce a value near zero, not near +16M
    // (which is what reading the top byte as unsigned would produce).
    data.insert(data.end(), {0xFFu, 0xFFu, 0xFFu});
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 1, 48000, 24, data);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 3u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), -32768)
        << "full-scale-negative 24-bit must sign-extend, not read as a large positive value";
    EXPECT_GT(ReadSample(decoded.samples, 1), 32000);
    EXPECT_EQ(ReadSample(decoded.samples, 2), 0)
        << "24-bit integer -1 is near zero, not full-scale negative";
}

TEST(WavDecoderTest, Pcm32TruncatesToTopSixteenBits)
{
    std::vector<std::uint8_t> data;
    AppendU32(data, 0x7FFFFFFFu); // max positive 32-bit
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 1, 48000, 32, data);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 1u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), 32767);
}

TEST(WavDecoderTest, IeeeFloat32ConvertsAndClampsOutOfRangeValues)
{
    std::vector<std::uint8_t> data;
    for (const float f : {0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f})
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &f, 4);
        data.insert(data.end(), bytes, bytes + 4);
    }
    const std::vector<std::uint8_t> wav = BuildPlainWav(3, 1, 44100, 32, data);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 6u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), 0);
    EXPECT_EQ(ReadSample(decoded.samples, 1), 32767);
    EXPECT_EQ(ReadSample(decoded.samples, 2), -32768);
    EXPECT_EQ(ReadSample(decoded.samples, 3), 32767) << "out-of-range float must clamp, not wrap";
    EXPECT_EQ(ReadSample(decoded.samples, 4), -32768);
    EXPECT_NEAR(ReadSample(decoded.samples, 5), 16384, 1);
}

// --- WAVE_FORMAT_EXTENSIBLE ----------------------------------------------------------------------

TEST(WavDecoderTest, ExtensiblePcmResolvesThroughSubFormatGuid)
{
    std::vector<std::uint8_t> extension;
    AppendU16(extension, 16u); // validBitsPerSample
    AppendU32(extension, 0u);  // channelMask
    // KSDATAFORMAT_SUBTYPE_PCM: 00000001-0000-0010-8000-00AA00389B71.
    AppendU32(extension, 1u);
    AppendU16(extension, 0u);
    AppendU16(extension, 0x0010u);
    const std::vector<std::uint8_t> guidTail = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    extension.insert(extension.end(), guidTail.begin(), guidTail.end());

    std::vector<std::uint8_t> data;
    AppendU16(data, 12345u);
    const std::vector<std::uint8_t> wav = BuildWavFromWaveFormatEx(
        data.data(), static_cast<std::uint32_t>(data.size()),
        /*wFormatTag=*/0xFFFE, 1, 44100, 88200, 2, 16, extension, 0u);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 1u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), 12345);
}

// --- MS-ADPCM and IMA-ADPCM -----------------------------------------------------------------------

TEST(WavDecoderTest, MsAdpcmRoundTripsWithinLossyBound)
{
    constexpr std::uint32_t sampleRate = 8000u;
    constexpr std::size_t frames = 400u;
    std::vector<std::int16_t> tone(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        tone[i] = static_cast<std::int16_t>(
            10000.0 * std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(i) / sampleRate));
    }
    const auto encoded = CNA::Internal::Audio::EncodeMsAdpcm(tone, 1u, 128u);
    const std::vector<std::uint8_t> wav = BuildWavFromWaveFormatEx(
        encoded.bytes.data(), static_cast<std::uint32_t>(encoded.bytes.size()),
        /*wFormatTag=*/2, 1, sampleRate, sampleRate, encoded.blockAlign, 4,
        CNA::Internal::Audio::MsAdpcmFormatExtension(encoded.samplesPerBlock), 0u);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.channels, 1u);
    ASSERT_GE(decoded.frameCount, frames);
    double worst = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        worst = std::max(worst, std::abs(static_cast<double>(tone[i]) - ReadSample(decoded.samples, i)));
    }
    EXPECT_LT(worst, 500.0) << "MS-ADPCM round trip drifted by " << worst;
}

TEST(WavDecoderTest, ImaAdpcmSilentBlockDecodesToDocumentedFrameCount)
{
    // Matches SoundEffectContentTypeReaderTests' ImaAdpcmLoopPointsSurviveAsDecodedFramesNotCompressedBytes
    // fixture geometry: 4 blocks of 256 bytes, mono, no wSamplesPerBlock extension (auto-derived).
    constexpr std::uint16_t blockAlign = 256u;
    constexpr int blockCount = 4;
    std::vector<std::uint8_t> data(static_cast<std::size_t>(blockAlign) * blockCount, 0u);
    const std::vector<std::uint8_t> wav = BuildWavFromWaveFormatEx(
        data.data(), static_cast<std::uint32_t>(data.size()),
        /*wFormatTag=*/0x0011, 1, 44100, 22343, blockAlign, 4, {}, 0u);
    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    constexpr int samplesPerBlock = (blockAlign - 4) * 8 / 4 + 1; // 505
    EXPECT_EQ(decoded.frameCount, static_cast<std::uint32_t>(samplesPerBlock * blockCount));
    // All-zero input is a legitimate differential-decode input: it must decode to silence, not throw.
    for (std::uint32_t i = 0; i < decoded.frameCount; ++i)
    {
        EXPECT_EQ(ReadSample(decoded.samples, i), 0) << "frame " << i;
    }
}

// --- malformed / truncated files -------------------------------------------------------------

TEST(WavDecoderTest, TruncatedRiffIsRefused)
{
    const std::vector<std::uint8_t> tooShort = {'R', 'I', 'F', 'F', 0, 0};
    EXPECT_THROW((void)DecodeWavToPcm16(tooShort, "test"), std::runtime_error);
}

TEST(WavDecoderTest, NotARiffFileIsRefused)
{
    const std::vector<std::uint8_t> notWav(20, 0xAB);
    EXPECT_THROW((void)DecodeWavToPcm16(notWav, "test"), std::runtime_error);
}

TEST(WavDecoderTest, TruncatedFormatChunkIsRefused)
{
    std::vector<std::uint8_t> wav;
    AppendTag(wav, "RIFF");
    AppendU32(wav, 20u);
    AppendTag(wav, "WAVE");
    AppendTag(wav, "fmt ");
    AppendU32(wav, 4u); // declares only 4 bytes, less than the required 16
    AppendU16(wav, 1u);
    AppendU16(wav, 1u);
    EXPECT_THROW((void)DecodeWavToPcm16(wav, "test"), std::runtime_error);
}

TEST(WavDecoderTest, MissingDataChunkIsRefused)
{
    std::vector<std::uint8_t> wav;
    AppendTag(wav, "RIFF");
    AppendU32(wav, 0u);
    AppendTag(wav, "WAVE");
    AppendTag(wav, "fmt ");
    AppendU32(wav, 16u);
    AppendU16(wav, 1u);
    AppendU16(wav, 1u);
    AppendU32(wav, 44100u);
    AppendU32(wav, 88200u);
    AppendU16(wav, 2u);
    AppendU16(wav, 16u);
    const std::uint32_t riffSize = static_cast<std::uint32_t>(wav.size() - 8u);
    std::memcpy(wav.data() + 4u, &riffSize, sizeof(riffSize));
    EXPECT_THROW((void)DecodeWavToPcm16(wav, "test"), std::runtime_error);
}

TEST(WavDecoderTest, EmptyDataChunkIsRefused)
{
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 1, 44100, 16, {});
    EXPECT_THROW((void)DecodeWavToPcm16(wav, "test"), std::runtime_error);
}

TEST(WavDecoderTest, InvalidBlockAlignmentIsRefused)
{
    // 16-bit mono declares a whole number of frames must be 2-byte multiples; 3 odd bytes of
    // payload cannot be.
    const std::vector<std::uint8_t> data = {1u, 2u, 3u};
    const std::vector<std::uint8_t> wav = BuildPlainWav(1, 1, 44100, 16, data);
    EXPECT_THROW((void)DecodeWavToPcm16(wav, "test"), std::runtime_error);
}

TEST(WavDecoderTest, ZeroChannelsIsRefused)
{
    std::vector<std::uint8_t> wav;
    AppendTag(wav, "RIFF");
    AppendU32(wav, 0u);
    AppendTag(wav, "WAVE");
    AppendTag(wav, "fmt ");
    AppendU32(wav, 16u);
    AppendU16(wav, 1u);
    AppendU16(wav, 0u); // channels = 0
    AppendU32(wav, 44100u);
    AppendU32(wav, 0u);
    AppendU16(wav, 0u);
    AppendU16(wav, 16u);
    AppendTag(wav, "data");
    AppendU32(wav, 4u);
    wav.insert(wav.end(), {1u, 2u, 3u, 4u});
    const std::uint32_t riffSize = static_cast<std::uint32_t>(wav.size() - 8u);
    std::memcpy(wav.data() + 4u, &riffSize, sizeof(riffSize));
    EXPECT_THROW((void)DecodeWavToPcm16(wav, "test"), std::runtime_error);
}

TEST(WavDecoderTest, UnsupportedEncodingIsRefusedByName)
{
    // wFormatTag 6 = A-law -- not decoded, and must be refused rather than misread as PCM.
    const std::vector<std::uint8_t> data(4, 0u);
    const std::vector<std::uint8_t> wav = BuildPlainWav(6, 1, 8000, 8, data);
    EXPECT_THROW((void)DecodeWavToPcm16(wav, "test"), std::runtime_error);
}

// --- ancillary chunks and padding -------------------------------------------------------------

TEST(WavDecoderTest, UnknownAncillaryChunkBeforeDataIsSkipped)
{
    std::vector<std::uint8_t> wav;
    AppendTag(wav, "RIFF");
    AppendU32(wav, 0u);
    AppendTag(wav, "WAVE");
    AppendTag(wav, "fmt ");
    AppendU32(wav, 16u);
    AppendU16(wav, 1u);
    AppendU16(wav, 1u);
    AppendU32(wav, 44100u);
    AppendU32(wav, 88200u);
    AppendU16(wav, 2u);
    AppendU16(wav, 16u);
    // An odd-sized unknown "JUNK" chunk, exercising the required padding byte before "data".
    AppendTag(wav, "JUNK");
    AppendU32(wav, 3u);
    wav.insert(wav.end(), {0xAAu, 0xBBu, 0xCCu});
    wav.push_back(0u); // pad byte
    AppendTag(wav, "data");
    AppendU32(wav, 2u);
    AppendU16(wav, 4321u);
    const std::uint32_t riffSize = static_cast<std::uint32_t>(wav.size() - 8u);
    std::memcpy(wav.data() + 4u, &riffSize, sizeof(riffSize));

    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 1u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), 4321);
}

TEST(WavDecoderTest, DataChunkBeforeAncillaryChunkIsAlsoValid)
{
    std::vector<std::uint8_t> wav;
    AppendTag(wav, "RIFF");
    AppendU32(wav, 0u);
    AppendTag(wav, "WAVE");
    AppendTag(wav, "fmt ");
    AppendU32(wav, 16u);
    AppendU16(wav, 1u);
    AppendU16(wav, 1u);
    AppendU32(wav, 44100u);
    AppendU32(wav, 88200u);
    AppendU16(wav, 2u);
    AppendU16(wav, 16u);
    AppendTag(wav, "data");
    AppendU32(wav, 2u);
    AppendU16(wav, 555u);
    AppendTag(wav, "LIST");
    AppendU32(wav, 4u);
    wav.insert(wav.end(), {'I', 'N', 'F', 'O'});
    const std::uint32_t riffSize = static_cast<std::uint32_t>(wav.size() - 8u);
    std::memcpy(wav.data() + 4u, &riffSize, sizeof(riffSize));

    const DecodedWavPcm16 decoded = DecodeWavToPcm16(wav, "test");
    ASSERT_EQ(decoded.frameCount, 1u);
    EXPECT_EQ(ReadSample(decoded.samples, 0), 555);
}
