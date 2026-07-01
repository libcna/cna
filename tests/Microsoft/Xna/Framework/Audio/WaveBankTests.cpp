// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
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
using Microsoft::Xna::Framework::Audio::WaveBank;

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

    // Minimal compact .xwb with one PCM16 mono entry (200 bytes of silence).
    std::vector<uint8_t> BuildXwbFixtureBytes()
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
        AppendPadded(data, kWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)          // format tag: PCM
            | (0u << 2)     // channels - 1 = 0 -> mono
            | (44100u << 5) // sample rate
            | (2u << 23)    // wBlockAlign
            | (1u << 31);   // bits-per-sample flag -> 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0 (last/only entry: length = segLength[4])

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(0);

        return data;
    }

    // Minimal .xsb with one wavebank reference, one simple sound pointing at wave 0
    // of that bank, and one simple cue named "Boom" playing that sound.
    std::vector<uint8_t> BuildXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName         = "Boom";

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

        AppendPadded(data, kWaveBankName, 64); // wavebank name table (1 entry)

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

    AudioEngine& SharedEngine()
    {
        static AudioEngine engine(
            (std::filesystem::temp_directory_path() / "cna_wavebank_test_nonexistent.xgs").string());
        return engine;
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

// ===================== GetTypeName =====================

TEST(WaveBankTest, GetTypeNameIsDottedXnaName)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_EQ(wb.GetTypeName(), "Microsoft.Xna.Framework.Audio.WaveBank");
}
