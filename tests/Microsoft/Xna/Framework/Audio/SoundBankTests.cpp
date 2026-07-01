// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Object.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;

namespace
{
    void AppendU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }

    void AppendU16(std::vector<uint8_t>& buf, uint16_t v)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendS32(std::vector<uint8_t>& buf, int32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendU32(std::vector<uint8_t>& buf, uint32_t v)
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

    // Minimal .xsb with zero wavebanks/sounds and one simple cue named "Explosion".
    // Cue::Play() cannot resolve an actual sound (soundCount==0), so it takes the
    // "no sound found" branch and still transitions to State::Playing — enough to
    // exercise GetCue/PlayCue/IsInUse without needing a full WaveBank round trip.
    std::vector<uint8_t> BuildXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138

        const uint32_t cueSimpleOffset    = baseOffset;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName         = "Explosion";

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
        AppendU8(data, 0);  // wavebankCount
        AppendU16(data, 0); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, static_cast<int32_t>(cueSimpleOffset));
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset (must sit at header byte 0x32)
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, -1); // soundOffset

        AppendPadded(data, "TestSoundBank", bankNameSize);

        // cueSimpleOffset: {flags: u8, sbCode: u32} — sbCode need not resolve to a
        // real sound since soundCount is 0; the parser defaults soundIndex to 0.
        AppendU8(data, 0);
        AppendU32(data, 0);

        // cueNameIndexOffset: {nameAbsOffset: u32, unknown: u16}
        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    const std::string& XsbFixturePath()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_soundbank_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xsb";
            const auto bytes = BuildXsbFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        return path;
    }

    // AudioEngine constructed against a nonexistent .xgs — runs as the documented
    // "stub" (no categories/variables), which is all SoundBank/Cue need here.
    AudioEngine& SharedEngine()
    {
        static AudioEngine engine(
            (std::filesystem::temp_directory_path() / "cna_soundbank_test_nonexistent.xgs").string());
        return engine;
    }
}

// ===================== Constructor =====================

TEST(SoundBankTest, ConstructorNullEngineThrowsArgumentNull)
{
    EXPECT_THROW(SoundBank bank(nullptr, XsbFixturePath()), System::ArgumentNullException);
}

TEST(SoundBankTest, ConstructorEmptyFilenameThrowsArgumentNull)
{
    EXPECT_THROW(SoundBank bank(&SharedEngine(), ""), System::ArgumentNullException);
}

TEST(SoundBankTest, ConstructorLoadsValidFixture)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_FALSE(bank.getIsDisposedProperty());
}

// ===================== IsDisposed / Dispose =====================

TEST(SoundBankTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_FALSE(bank.getIsDisposedProperty());
    bank.Dispose();
    EXPECT_TRUE(bank.getIsDisposedProperty());
}

TEST(SoundBankTest, DisposeIsIdempotent)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.Dispose();
    EXPECT_NO_THROW(bank.Dispose());
    EXPECT_TRUE(bank.getIsDisposedProperty());
}

TEST(SoundBankTest, DisposeRaisesDisposingEvent)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bool raised = false;
    bank.Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    bank.Dispose();
    EXPECT_TRUE(raised);
}

// ===================== GetCue =====================

TEST(SoundBankTest, GetCueValidReturnsMatchingName)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    std::unique_ptr<Cue> cue(bank.GetCue("Explosion"));
    ASSERT_NE(cue, nullptr);
    EXPECT_EQ(cue->getNameProperty(), "Explosion");
}

TEST(SoundBankTest, GetCueEmptyNameThrowsArgumentNull)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_THROW((void)bank.GetCue(""), System::ArgumentNullException);
}

TEST(SoundBankTest, GetCueInvalidNameThrowsInvalidOperation)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_THROW((void)bank.GetCue("NoSuchCue"), System::InvalidOperationException);
}

TEST(SoundBankTest, GetCueAfterDisposeThrowsObjectDisposed)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.Dispose();
    EXPECT_THROW((void)bank.GetCue("Explosion"), System::ObjectDisposedException);
}

// ===================== PlayCue (2-arg) =====================

TEST(SoundBankTest, PlayCueTwoArgValidDoesNotThrow)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_NO_THROW(bank.PlayCue("Explosion"));
}

TEST(SoundBankTest, PlayCueTwoArgEmptyNameThrowsArgumentNull)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_THROW(bank.PlayCue(""), System::ArgumentNullException);
}

TEST(SoundBankTest, PlayCueTwoArgInvalidNameThrowsInvalidOperation)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_THROW(bank.PlayCue("NoSuchCue"), System::InvalidOperationException);
}

TEST(SoundBankTest, PlayCueTwoArgAfterDisposeThrowsObjectDisposed)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.Dispose();
    EXPECT_THROW(bank.PlayCue("Explosion"), System::ObjectDisposedException);
}

// ===================== PlayCue (3-arg) =====================

TEST(SoundBankTest, PlayCueThreeArgValidDoesNotThrow)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_NO_THROW(bank.PlayCue("Explosion", listener, emitter));
}

TEST(SoundBankTest, PlayCueThreeArgInvalidNameThrowsInvalidOperation)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_THROW(bank.PlayCue("NoSuchCue", listener, emitter), System::InvalidOperationException);
}

// ===================== IsInUse =====================

TEST(SoundBankTest, IsInUseFalseWithNoFireAndForgetCues)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_FALSE(bank.getIsInUseProperty());
}

TEST(SoundBankTest, IsInUseTrueAfterPlayCueThenFalseAfterDispose)
{
    // Fire-and-forget cues are only swept by elapsed time (>=5s) on the next PlayCue
    // call, so Dispose (which clears them immediately) is used here as the
    // deterministic "stop" trigger instead of a real-time sleep.
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_TRUE(bank.getIsInUseProperty());

    bank.Dispose();
    EXPECT_FALSE(bank.getIsInUseProperty());
}

// ===================== GetTypeName =====================

TEST(SoundBankTest, GetTypeNameIsDottedXnaName)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_EQ(bank.GetTypeName(), "Microsoft.Xna.Framework.Audio.SoundBank");
}
