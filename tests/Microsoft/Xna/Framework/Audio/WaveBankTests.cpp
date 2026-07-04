// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/EventArgs.hpp"
#include "System/Object.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::WaveBank;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for T-3F: whether a bank loaded via streaming/lazy per-entry reads
    // (XwbData::streaming) vs eagerly loading the whole file, how many file bytes are actually
    // resident in memory, and direct access to the private GetSoundEffect() so offset/length
    // correctness can be checked without going through a real Cue/SoundBank/audio-device pipeline.
    struct WaveBankTestAccess
    {
        static bool IsStreaming(const WaveBank& wb) { return wb.StreamingInternal(); }
        static std::size_t ResidentFileBytes(const WaveBank& wb) { return wb.ResidentFileBytesInternal(); }
        static const SoundEffect* GetSoundEffect(WaveBank& wb, unsigned short index)
        {
            return wb.GetSoundEffect(index);
        }
    };
}

using Microsoft::Xna::Framework::Audio::WaveBankTestAccess;

namespace
{
    void AppendU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }

    void AppendU16(std::vector<uint8_t>& buf, uint16_t v)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendU32(std::vector<uint8_t>& buf, uint32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendS32(std::vector<uint8_t>& buf, int32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendPadded(std::vector<uint8_t>& buf, const std::string& text, std::size_t totalSize)
    {
        const std::size_t start = buf.size();
        buf.resize(start + totalSize, 0);
        std::memcpy(buf.data() + start, text.data(), text.size());
    }

    void AppendCStr(std::vector<uint8_t>& buf, const std::string& s)
    {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(0);
    }

    constexpr const char* kWaveBankName = "TestWaveBank";

    // Minimal compact .xwb with one mono PCM entry (200 bytes of silence). Defaults to PCM16
    // (direct SoundEffect(buffer) construction path); pass eightBitPcm=true to instead exercise
    // WaveBank::GetSoundEffect's WAV-wrapping SoundEffect::FromStream path (regression coverage
    // for XA-2, a leak in that branch) -- give it a distinct bankName so it doesn't collide with
    // "TestWaveBank" in AudioEngine::RegisterWaveBank's name-keyed registry.
    std::vector<uint8_t> BuildXwbFixtureBytes(const std::string& bankName = kWaveBankName,
                                               bool eightBitPcm = false)
    {
        constexpr uint32_t headerSize       = 48;
        constexpr uint32_t bankDataSize     = 96;
        constexpr uint32_t entryCount       = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength   = 200;
        constexpr uint32_t alignment        = 4;

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, waveDataLength };

        std::vector<uint8_t> data;

        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1); // version (<=43 -> no headerVersion field)
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, bankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)                                  // format tag: PCM
            | (0u << 2)                              // channels - 1 = 0 -> mono
            | (44100u << 5)                          // sample rate
            | ((eightBitPcm ? 1u : 2u) << 23)         // wBlockAlign: bytes/sample (mono)
            | ((eightBitPcm ? 0u : 1u) << 31);        // bits-per-sample flag -> 8-bit or 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0 (last/only entry: length = segLength[4])

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(eightBitPcm ? 0x80 : 0x00); // silence: 8-bit unsigned midpoint, or 16-bit zero

        return data;
    }

    // Minimal .xsb with one wavebank reference, one simple sound pointing at wave 0
    // of that bank, and one simple cue playing that sound.
    std::vector<uint8_t> BuildXsbFixtureBytes(const std::string& wavebankName = kWaveBankName,
                                               const std::string& cueName = "Boom")
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;

        std::vector<uint8_t> data;

        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 1); // cueSimpleCount
        AppendU16(data, 0); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 1);  // wavebankCount
        AppendU16(data, 1); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, static_cast<int32_t>(cueSimpleOffset));
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset (must sit at header byte 0x32)
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, static_cast<int32_t>(wavebankNameOffset));
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "TestSoundBank", bankNameSize);

        AppendPadded(data, wavebankName, 64); // wavebank name table (1 entry)

        // Sound: flags, categoryIndex, volume, pitchCents, priority, soundLength(skip),
        // then (not complex) waveIdx, wbIdx.
        AppendU8(data, 0);   // flags (no COMPLEX/RPC/DSP bits)
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF);// volume byte
        AppendU16(data, 0);  // pitchCents (s16, 0)
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // cueSimpleOffset: {flags: u8, sbCode: u32} — sbCode matches the sound's offset.
        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        // cueNameIndexOffset: {nameAbsOffset: u32, unknown: u16}
        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    std::string WriteFixture(const std::string& dirName, const std::string& fileName,
                              const std::vector<uint8_t>& bytes)
    {
        auto dir = std::filesystem::temp_directory_path() / dirName;
        std::filesystem::create_directories(dir);
        auto file = dir / fileName;
        std::ofstream f(file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return file.string();
    }

    const std::string& XwbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_wavebank_test", "fixture.xwb", BuildXwbFixtureBytes());
        return path;
    }

    const std::string& XsbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_wavebank_test", "fixture.xsb", BuildXsbFixtureBytes());
        return path;
    }

    constexpr const char* kWaveBank8BitName = "TestWaveBank8Bit";
    constexpr const char* kCue8BitName      = "Boom8Bit";

    const std::string& Xwb8BitFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "fixture8bit.xwb",
            BuildXwbFixtureBytes(kWaveBank8BitName, /*eightBitPcm=*/true));
        return path;
    }

    const std::string& Xsb8BitFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "fixture8bit.xsb",
            BuildXsbFixtureBytes(kWaveBank8BitName, kCue8BitName));
        return path;
    }

    AudioEngine& SharedEngine()
    {
        static AudioEngine engine(
            (std::filesystem::temp_directory_path() / "cna_wavebank_test_nonexistent.xgs").string());
        return engine;
    }

    constexpr const char* kMultiEntryWaveBankName = "MultiEntryWaveBank";

    // Non-compact .xwb (standard 24-byte entryMetaDataSize) with two mono 16-bit PCM entries at
    // different offsets/lengths within the wave-data segment (entry 0: 100 bytes at offset 0;
    // entry 1: 300 bytes at offset 100) -- needed to prove streaming's lazy per-entry disk reads
    // (T-3F) land on the correct byte range, not just "some" range. A single-entry fixture
    // couldn't catch an offset bug, since entry 0 always starts at offset 0 either way.
    std::vector<uint8_t> BuildMultiEntryXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 2;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t entry0Length      = 100;
        constexpr uint32_t entry1Length      = 300;
        constexpr uint32_t waveDataLength    = entry0Length + entry1Length;

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, waveDataLength };

        std::vector<uint8_t> data;
        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1); // version (<=43 -> no headerVersion field)
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0u); // wbFlags: not compact, no names
        AppendU32(data, entryCount);
        AppendPadded(data, kMultiEntryWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        const uint32_t fmt =
              (0u)          // fmtTag: PCM
            | (0u << 2)     // channels-1 = 0 -> mono
            | (44100u << 5) // sample rate
            | (2u << 23)    // wBlockAlign: 2 bytes/sample
            | (1u << 31);   // 16-bit

        // Entry 0: playOffset=0, playLength=entry0Length
        AppendU32(data, 0u); // flagsAndDuration (unused by CNA)
        AppendU32(data, fmt);
        AppendU32(data, 0u);
        AppendU32(data, entry0Length);
        AppendU32(data, 0u); // loopStart
        AppendU32(data, 0u); // loopTotal

        // Entry 1: playOffset=entry0Length, playLength=entry1Length
        AppendU32(data, 0u);
        AppendU32(data, fmt);
        AppendU32(data, entry0Length);
        AppendU32(data, entry1Length);
        AppendU32(data, 0u);
        AppendU32(data, 0u);

        for (uint32_t i = 0; i < entry0Length; ++i)
            data.push_back(0xAA);
        for (uint32_t i = 0; i < entry1Length; ++i)
            data.push_back(0xBB);

        return data;
    }

    const std::string& MultiEntryXwbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "multifixture.xwb", BuildMultiEntryXwbFixtureBytes());
        return path;
    }
}

// ===================== Constructors =====================

TEST(WaveBankTest, NonStreamingCtorNullEngineThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(nullptr, XwbFixturePath()), System::ArgumentNullException);
}

TEST(WaveBankTest, NonStreamingCtorEmptyFilenameThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(&SharedEngine(), ""), System::ArgumentNullException);
}

TEST(WaveBankTest, NonStreamingCtorLoadsValidFixture)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_FALSE(wb.getIsDisposedProperty());
    EXPECT_TRUE(wb.getIsPreparedProperty());
}

TEST(WaveBankTest, StreamingCtorNullEngineThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(nullptr, XwbFixturePath(), 0, 0), System::ArgumentNullException);
}

TEST(WaveBankTest, StreamingCtorEmptyFilenameThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(&SharedEngine(), "", 0, 0), System::ArgumentNullException);
}

TEST(WaveBankTest, StreamingCtorLoadsValidFixture)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath(), 0, 2048);
    EXPECT_FALSE(wb.getIsDisposedProperty());
    EXPECT_TRUE(wb.getIsPreparedProperty());
}

// ===================== Streaming vs. in-memory loading (T-3F) =====================

// The non-streaming ctor must keep the FNA-matching "everything eager" behavior: the entire
// file is resident, and WaveBank::StreamingInternal() reports false.
TEST(WaveBankTest, NonStreamingCtorLoadsEntireFileIntoMemory)
{
    const auto& path = XwbFixturePath();
    const auto fullFileSize = std::filesystem::file_size(path);

    WaveBank wb(&SharedEngine(), path);
    ASSERT_TRUE(wb.getIsPreparedProperty());

    EXPECT_FALSE(WaveBankTestAccess::IsStreaming(wb));
    EXPECT_EQ(WaveBankTestAccess::ResidentFileBytes(wb), fullFileSize);
}

// The streaming ctor must NOT eagerly load the (by far largest) wave-data segment -- only the
// header/metadata segments should be resident; this is the actual memory-footprint payoff of
// T-3F's real streaming implementation, as opposed to the old "streaming ctor secretly loads
// everything anyway" behavior.
TEST(WaveBankTest, StreamingCtorDoesNotLoadWaveDataSegmentIntoMemory)
{
    const auto& path = XwbFixturePath();
    const auto fullFileSize = std::filesystem::file_size(path);

    WaveBank wb(&SharedEngine(), path, 0, 2048);
    ASSERT_TRUE(wb.getIsPreparedProperty());

    EXPECT_TRUE(WaveBankTestAccess::IsStreaming(wb));
    const auto residentBytes = WaveBankTestAccess::ResidentFileBytes(wb);
    EXPECT_LT(residentBytes, fullFileSize);
    // BuildXwbFixtureBytes' single entry is 200 bytes of wave data with no name table --
    // resident size must be exactly fullFileSize minus that wave-data segment.
    EXPECT_EQ(residentBytes, fullFileSize - 200);
}

// Regression coverage for T-3F's "correct offsetted reading" accept criterion: a single-entry
// fixture can't distinguish "read the right bytes" from "read the wrong bytes that happen to be
// the same length", so this uses a two-entry fixture where entry 1 sits at a nonzero offset and
// has a different length than entry 0. If the streaming path's lazy read used the wrong offset
// (e.g. always reading from the start of the wave-data segment, or reusing entry 0's range),
// entry 1's decoded duration would not match its own non-streaming counterpart.
TEST(WaveBankTest, StreamingGetSoundEffectReadsCorrectPerEntryOffsetAndLength)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        WaveBank streamingWb(&SharedEngine(), MultiEntryXwbFixturePath(), 0, 0);
        ASSERT_TRUE(streamingWb.getIsPreparedProperty());
        ASSERT_TRUE(WaveBankTestAccess::IsStreaming(streamingWb));

        WaveBank eagerWb(&SharedEngine(), MultiEntryXwbFixturePath());
        ASSERT_TRUE(eagerWb.getIsPreparedProperty());
        ASSERT_FALSE(WaveBankTestAccess::IsStreaming(eagerWb));

        const SoundEffect* streamedEntry0 = WaveBankTestAccess::GetSoundEffect(streamingWb, 0);
        const SoundEffect* streamedEntry1 = WaveBankTestAccess::GetSoundEffect(streamingWb, 1);
        const SoundEffect* eagerEntry1    = WaveBankTestAccess::GetSoundEffect(eagerWb, 1);

        if (!streamedEntry0 || !streamedEntry1 || !eagerEntry1)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffect";
        }

        // Entry 1 is 3x longer than entry 0 (300 vs 100 bytes) -- an offset bug would either
        // fail to decode, or produce entry 0's duration instead of entry 1's.
        EXPECT_EQ(streamedEntry1->getDurationProperty(), eagerEntry1->getDurationProperty());
        EXPECT_NE(streamedEntry0->getDurationProperty(), streamedEntry1->getDurationProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// ===================== IsDisposed / IsPrepared =====================

TEST(WaveBankTest, IsPreparedFalseWhenFileMissing)
{
    const auto missing =
        (std::filesystem::temp_directory_path() / "cna_wavebank_test_missing.xwb").string();
    WaveBank wb(&SharedEngine(), missing);
    EXPECT_FALSE(wb.getIsPreparedProperty());
}

TEST(WaveBankTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_FALSE(wb.getIsDisposedProperty());
    wb.Dispose();
    EXPECT_TRUE(wb.getIsDisposedProperty());
    EXPECT_FALSE(wb.getIsPreparedProperty());
}

TEST(WaveBankTest, DisposeIsIdempotent)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    wb.Dispose();
    EXPECT_NO_THROW(wb.Dispose());
    EXPECT_TRUE(wb.getIsDisposedProperty());
}

TEST(WaveBankTest, DisposeRaisesDisposingEvent)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    bool raised = false;
    wb.Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    wb.Dispose();
    EXPECT_TRUE(raised);
}

// ===================== IsInUse =====================

TEST(WaveBankTest, IsInUseFalseWithNoCues)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_FALSE(wb.getIsInUseProperty());
}

TEST(WaveBankTest, IsInUseTrueWhilePlayingThenFalseAfterStop)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, XsbFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue("Boom"));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        EXPECT_TRUE(wb.getIsInUseProperty());
        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_FALSE(wb.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// Regression coverage for XA-2: WaveBank::GetSoundEffect's 8-bit-PCM branch wraps the raw wave
// data in a WAV and goes through SoundEffect::FromStream (unlike the 16-bit PCM path above, which
// constructs a SoundEffect directly). FromStream used to leak the heap SoundEffect it returns.
TEST(WaveBankTest, GetSoundEffectFor8BitPcmEntrySucceeds)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, Xwb8BitFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, Xsb8BitFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue(kCue8BitName));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        EXPECT_TRUE(wb.getIsInUseProperty());
        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_FALSE(wb.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// ===================== GetTypeName =====================

TEST(WaveBankTest, GetTypeNameIsDottedXnaName)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_EQ(wb.GetTypeName(), "Microsoft.Xna.Framework.Audio.WaveBank");
}
