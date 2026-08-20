// SPDX-License-Identifier: MS-PL
//
// plans/plan_audio.md AUD-11-027: direct, field-by-field validation of BuildWavFromWaveFormatEx's own
// output bytes -- every other test in this codebase that exercises this shared WAV-wrapper
// (WaveBank's PCM8/ADPCM entries, the XNB SoundEffectReader's non-PCM16 paths) only checks it
// indirectly, via "did SDL3 successfully decode it." That proves the output is *decodable*, not
// that every individual field (RIFF size, fmt chunk contents, fact chunk, data chunk) is exactly
// correct -- a wrapper could still produce a technically-decodable-but-subtly-wrong file (e.g. an
// off-by-N RIFF size that SDL's lenient parser tolerates) without any existing test catching it.

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#include "CNA/Internal/Audio/WavWrapper.hpp"

using CNA::Internal::Audio::AppendSmplChunkIfLooped;
using CNA::Internal::Audio::BuildStandardMsAdpcmExtension;
using CNA::Internal::Audio::BuildWavFromWaveFormatEx;

namespace
{
    uint16_t ReadU16(const std::vector<uint8_t>& v, std::size_t offset)
    {
        uint16_t x;
        std::memcpy(&x, v.data() + offset, 2);
        return x;
    }
    uint32_t ReadU32(const std::vector<uint8_t>& v, std::size_t offset)
    {
        uint32_t x;
        std::memcpy(&x, v.data() + offset, 4);
        return x;
    }
    std::string ReadTag(const std::vector<uint8_t>& v, std::size_t offset)
    {
        return std::string(reinterpret_cast<const char*>(v.data() + offset), 4);
    }
}

TEST(WavWrapperTest, PlainPcmNoExtensionNoFactProducesExactly44ByteHeaderPlusData)
{
    const std::vector<uint8_t> audio(10, 0xAB);
    auto wav = BuildWavFromWaveFormatEx(
        audio.data(), static_cast<uint32_t>(audio.size()),
        /*wFormatTag=*/1, /*channels=*/1, /*sampleRate=*/44100, /*avgBytesPerSec=*/88200,
        /*blockAlign=*/2, /*bitsPerSample=*/16, /*extensionData=*/{}, /*factSampleFrames=*/0);

    // RIFF(4) + size(4) + WAVE(4) + fmt (4) + fmtSize(4) + fmt-fields(16) + data(4) + dataSize(4)
    // + audio(10) = 44 bytes total, no extension, no fact chunk.
    ASSERT_EQ(wav.size(), 44u + audio.size());

    EXPECT_EQ(ReadTag(wav, 0), "RIFF");
    EXPECT_EQ(ReadU32(wav, 4), static_cast<uint32_t>(wav.size() - 8)); // RIFF size excludes RIFF+size itself
    EXPECT_EQ(ReadTag(wav, 8), "WAVE");

    EXPECT_EQ(ReadTag(wav, 12), "fmt ");
    EXPECT_EQ(ReadU32(wav, 16), 16u); // plain WAVEFORMAT, no cbSize
    EXPECT_EQ(ReadU16(wav, 20), 1u);     // wFormatTag
    EXPECT_EQ(ReadU16(wav, 22), 1u);     // channels
    EXPECT_EQ(ReadU32(wav, 24), 44100u); // sampleRate
    EXPECT_EQ(ReadU32(wav, 28), 88200u); // avgBytesPerSec
    EXPECT_EQ(ReadU16(wav, 32), 2u);     // blockAlign
    EXPECT_EQ(ReadU16(wav, 34), 16u);    // bitsPerSample

    EXPECT_EQ(ReadTag(wav, 36), "data");
    EXPECT_EQ(ReadU32(wav, 40), static_cast<uint32_t>(audio.size()));
    EXPECT_EQ(std::memcmp(wav.data() + 44, audio.data(), audio.size()), 0);
}

TEST(WavWrapperTest, ExtensionDataIsCopiedVerbatimWithCorrectCbSize)
{
    const std::vector<uint8_t> audio(4, 0);
    const std::vector<uint8_t> extension = BuildStandardMsAdpcmExtension(500);
    ASSERT_EQ(extension.size(), 32u); // wSamplesPerBlock(2) + wNumCoef(2) + 7 coeff pairs(28)

    auto wav = BuildWavFromWaveFormatEx(
        audio.data(), static_cast<uint32_t>(audio.size()),
        /*wFormatTag=*/2, /*channels=*/1, /*sampleRate=*/44100, /*avgBytesPerSec=*/22343,
        /*blockAlign=*/256, /*bitsPerSample=*/4, extension, /*factSampleFrames=*/0);

    // fmt chunk is now 18 + 32 = 50 bytes (cbSize-prefixed extension).
    EXPECT_EQ(ReadU32(wav, 16), 50u);
    EXPECT_EQ(ReadU16(wav, 36), static_cast<uint16_t>(extension.size())); // cbSize field

    // Extension bytes start right after cbSize (offset 38) and must match verbatim.
    EXPECT_EQ(std::memcmp(wav.data() + 38, extension.data(), extension.size()), 0);

    // data chunk follows immediately after the 18+32-byte fmt chunk.
    const std::size_t dataTagOffset = 12 + 8 + 50; // "fmt " header start + chunk header + fmtSize
    EXPECT_EQ(ReadTag(wav, dataTagOffset), "data");
    EXPECT_EQ(ReadU32(wav, dataTagOffset + 4), static_cast<uint32_t>(audio.size()));
}

TEST(WavWrapperTest, FactChunkPresentOnlyWhenSampleFramesNonzero)
{
    const std::vector<uint8_t> audio(8, 0);

    auto withoutFact = BuildWavFromWaveFormatEx(
        audio.data(), static_cast<uint32_t>(audio.size()), 1, 1, 44100, 88200, 2, 16, {}, 0);
    // No "fact" tag anywhere in a fact-less file.
    bool foundFact = false;
    for (std::size_t i = 0; i + 4 <= withoutFact.size(); ++i)
        if (std::memcmp(withoutFact.data() + i, "fact", 4) == 0) foundFact = true;
    EXPECT_FALSE(foundFact);

    auto withFact = BuildWavFromWaveFormatEx(
        audio.data(), static_cast<uint32_t>(audio.size()), 2, 1, 44100, 22343, 256, 4,
        BuildStandardMsAdpcmExtension(500), /*factSampleFrames=*/12345);

    // fmt chunk is 18+32=50 bytes; "fact" chunk immediately follows.
    const std::size_t factTagOffset = 12 + 8 + 50;
    EXPECT_EQ(ReadTag(withFact, factTagOffset), "fact");
    EXPECT_EQ(ReadU32(withFact, factTagOffset + 4), 4u); // fact chunk payload size
    EXPECT_EQ(ReadU32(withFact, factTagOffset + 8), 12345u); // dwSampleLength

    // data chunk follows immediately after the fact chunk (8-byte header + 4-byte payload).
    const std::size_t dataTagOffset = factTagOffset + 8 + 4;
    EXPECT_EQ(ReadTag(withFact, dataTagOffset), "data");
}

TEST(WavWrapperTest, RiffSizeAccountsForEveryChunkIncludingFactAndExtension)
{
    const std::vector<uint8_t> audio(17, 0); // deliberately odd length
    auto wav = BuildWavFromWaveFormatEx(
        audio.data(), static_cast<uint32_t>(audio.size()), 2, 2, 44100, 44686, 512, 4,
        BuildStandardMsAdpcmExtension(1000), /*factSampleFrames=*/999);

    EXPECT_EQ(ReadU32(wav, 4), static_cast<uint32_t>(wav.size() - 8));
    // Sanity: the file's total real byte count matches what RIFF claims plus the 8 bytes
    // ("RIFF" + size field) that are themselves excluded from the RIFF size.
    EXPECT_EQ(ReadU32(wav, 4) + 8u, wav.size());
}

TEST(WavWrapperTest, AppendSmplChunkIfLoopedEncodesStartAndEndCorrectly)
{
    std::vector<uint8_t> wav = BuildWavFromWaveFormatEx(
        nullptr, 0, 1, 1, 44100, 88200, 2, 16, {}, 0);
    const std::size_t sizeBeforeSmpl = wav.size();

    AppendSmplChunkIfLooped(wav, /*loopStart=*/100, /*loopLength=*/50);

    ASSERT_EQ(wav.size(), sizeBeforeSmpl + 8 + 36 + 24); // chunk header + fixed body + 1 loop entry
    EXPECT_EQ(ReadTag(wav, sizeBeforeSmpl), "smpl");
    EXPECT_EQ(ReadU32(wav, sizeBeforeSmpl + 4), 36u + 24u); // chunk size

    const std::size_t loopEntryOffset = sizeBeforeSmpl + 8 + 36;
    // Loop entry: CuePointID(4), Type(4), Start(4), End(4), Fraction(4), PlayCount(4).
    EXPECT_EQ(ReadU32(wav, loopEntryOffset + 8), 100u);       // Start
    EXPECT_EQ(ReadU32(wav, loopEntryOffset + 12), 150u);      // End = start + length
    EXPECT_EQ(ReadU32(wav, loopEntryOffset + 20), 0u);        // PlayCount: 0 = infinite
}

TEST(WavWrapperTest, AppendSmplChunkIfLoopedIsNoOpForZeroOrNegativeLength)
{
    std::vector<uint8_t> wav = BuildWavFromWaveFormatEx(
        nullptr, 0, 1, 1, 44100, 88200, 2, 16, {}, 0);
    const std::size_t sizeBefore = wav.size();

    AppendSmplChunkIfLooped(wav, 100, 0);
    EXPECT_EQ(wav.size(), sizeBefore);

    AppendSmplChunkIfLooped(wav, 100, -5);
    EXPECT_EQ(wav.size(), sizeBefore);
}
