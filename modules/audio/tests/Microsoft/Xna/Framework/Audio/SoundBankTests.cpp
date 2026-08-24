// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CueTestAccess.hpp"
#include "SoundBankTestAccess.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Environment.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Object.hpp"
#include "System/ObjectDisposedException.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <SDL3_mixer/SDL_mixer.h>
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::WaveBank;

using Microsoft::Xna::Framework::Audio::SoundBankTestAccess;
using Microsoft::Xna::Framework::Audio::CueTestAccess;

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

    // Minimal parseable .xgs: zero categories/variables/rpcs, padded to ParseXgs's 0x50-byte
    // minimum (XactParser.cpp). AudioEngine's ctor now throws FileNotFoundException on a missing
    // settings file (P9-HARDWARE-003, matching FNA's TitleContainer.ReadToPointer), so this must
    // be a real, existing, parseable file rather than a deliberately-nonexistent path -- it still
    // has no categories/variables, which is all SoundBank/Cue need here.
    std::vector<uint8_t> BuildMinimalXgsFixtureBytes()
    {
        std::vector<uint8_t> data;
        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0); // categoryCount
        AppendU16(data, 0); // variableCount
        AppendU16(data, 0); // blob1Count
        AppendU16(data, 0); // blob2Count
        AppendU16(data, 0); // rpcCount
        AppendU16(data, 0); // dspPresetCount
        AppendU16(data, 0); // dspParameterCount

        for (int i = 0; i < 9; ++i) AppendU32(data, 0); // all nine offset fields

        data.resize(0x50, 0); // ParseXgs requires >= 0x50 bytes total
        return data;
    }

    AudioEngine& SharedEngine()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_soundbank_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture_engine.xgs";
            const auto bytes = BuildMinimalXgsFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        static AudioEngine engine(path);
        return engine;
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

    constexpr const char* kApply3DWaveBankName = "Apply3DWaveBank";

    // Minimal compact .xwb with one mono 16-bit PCM entry (200 bytes of silence) -- needed
    // (unlike BuildXsbFixtureBytes above) so that Cue::Play() resolves a real WaveBank entry and
    // creates an actual SoundEffectInstance, which T-4B's PlayCue-3D geometry test needs a real
    // MIX_Track to inspect.
    std::vector<uint8_t> BuildApply3DXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 200;
        constexpr uint32_t alignment         = 4;

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
        AppendU32(data, 1); // version
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, kApply3DWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)            // format tag: PCM
            | (1u << 2)       // channels: raw field IS the real channel count -> mono (IN-7)
            | (44100u << 5)   // sample rate
            | (2u << 23)      // wBlockAlign: 2 bytes/sample
            | (1u << 31);     // 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0 (last/only entry)

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(0x00); // 16-bit silence

        return data;
    }

    // Minimal .xsb with one wavebank reference, one simple sound (categoryIndex=0), and one
    // simple cue "Apply3DCue" playing that sound.
    std::vector<uint8_t> BuildApply3DXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "Apply3DCue";

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
        AppendS32(data, -1); // variationOffset
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, static_cast<int32_t>(wavebankNameOffset));
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "Apply3DSoundBank", bankNameSize);
        AppendPadded(data, kApply3DWaveBankName, 64);

        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    WaveBank& SharedApply3DWaveBank()
    {
        static WaveBank wb(&SharedEngine(), WriteFixture(
            "cna_soundbank_test", "apply3d.xwb", BuildApply3DXwbFixtureBytes()));
        return wb;
    }

    const std::string& Apply3DXsbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_soundbank_test", "apply3d.xsb", BuildApply3DXsbFixtureBytes());
        return path;
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

// P9-HARDWARE-003: matches FNA exactly (SoundBank.cs) -- a missing filename throws
// FileNotFoundException (from TitleContainer.ReadToPointer) before any FACT call is made.
TEST(SoundBankTest, ConstructorMissingFileThrowsFileNotFound)
{
    const auto missing =
        (std::filesystem::temp_directory_path() / "cna_soundbank_test_missing.xsb").string();
    EXPECT_THROW(SoundBank bank(&SharedEngine(), missing), System::IO::FileNotFoundException);
}

TEST(SoundBankTest, ConstructorLoadsValidFixture)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_FALSE(bank.getIsDisposedProperty());
}

TEST(SoundBankTest, ConstructorResolvesXnaCaseInsensitiveBankPath)
{
    const std::filesystem::path exact(XsbFixturePath());
    const auto requested = exact.parent_path().parent_path() /
                           "CNA_SOUNDBANK_TEST" / "FIXTURE.XSB";

    SoundBank bank(&SharedEngine(), requested.string());
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

// XA-13: a file that EXISTS but isn't a valid .xsb (bad magic/garbage) must behave the same as
// a missing/empty one -- construction doesn't throw, GetCue() on any name then throws
// InvalidOperationException (matching GetCueInvalidNameThrowsInvalidOperation's error), not some
// other unhandled exception. (XA-9 decided to keep this "silent stub" behavior rather than
// throw; this locks in that decision explicitly, on the "exists but corrupt" path specifically.)
TEST(SoundBankTest, ConstructorWithExistingButCorruptFileStaysInStubState)
{
    const auto corrupt = WriteFixture("cna_soundbank_test", "corrupt.xsb",
                                       {'n', 'o', 't', ' ', 'a', ' ', 's', 'o', 'u', 'n', 'd', 'b', 'a', 'n', 'k'});
    SoundBank bank(&SharedEngine(), corrupt);
    EXPECT_FALSE(bank.getIsDisposedProperty());
    EXPECT_THROW((void)bank.GetCue("Explosion"), System::InvalidOperationException);
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

// P10-XACT-007: the 3-arg overload shares PlayCueInternal with the 2-arg one (tested above via
// PlayCueTwoArgAfterDisposeThrowsObjectDisposed), but per this project's overload-testing
// convention each overload gets its own dedicated case rather than assuming shared internals.
TEST(SoundBankTest, PlayCueThreeArgAfterDisposeThrowsObjectDisposed)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    AudioListener listener;
    AudioEmitter emitter;
    bank.Dispose();
    EXPECT_THROW(bank.PlayCue("Explosion", listener, emitter), System::ObjectDisposedException);
}

// T-4B: the 3D PlayCue overload must actually reach Cue::Apply3D on the resulting
// fire-and-forget cue, not just accept listener/emitter without using them. XsbFixtureBytes'
// wavebank-less "Explosion" cue can't exercise this (Cue::active_ stays empty), so this uses a
// real WaveBank-backed fixture instead and reads back the actual SDL_mixer track gain.
TEST(SoundBankTest, PlayCueThreeArgAppliesRealAttenuationToActiveInstance)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        (void)SharedApply3DWaveBank(); // must be registered with the engine before PlayCue()
        SoundBank bank(&SharedEngine(), Apply3DXsbFixturePath());
        AudioListener listener; // default position: origin

        AudioEmitter nearEmitter;
        nearEmitter.setPositionProperty({1.0f, 0.0f, 0.0f});
        bank.PlayCue("Apply3DCue", listener, nearEmitter);

        Cue* nearCue = SoundBankTestAccess::LastFireAndForgetCue(bank);
        ASSERT_NE(nearCue, nullptr);
        SoundEffectInstance* nearInst = CueTestAccess::ActiveInstance(*nearCue, 0);
        if (!nearInst)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        MIX_Track* nearTrack = SoundEffectInstanceTestAccess::GetTrack(*nearInst);
        ASSERT_NE(nearTrack, nullptr);
        const float nearGain = MIX_GetTrackGain(nearTrack);

        AudioEmitter farEmitter;
        farEmitter.setPositionProperty({10000.0f, 0.0f, 0.0f});
        bank.PlayCue("Apply3DCue", listener, farEmitter);

        Cue* farCue = SoundBankTestAccess::LastFireAndForgetCue(bank);
        ASSERT_NE(farCue, nullptr);
        SoundEffectInstance* farInst = CueTestAccess::ActiveInstance(*farCue, 0);
        ASSERT_NE(farInst, nullptr);
        MIX_Track* farTrack = SoundEffectInstanceTestAccess::GetTrack(*farInst);
        ASSERT_NE(farTrack, nullptr);
        const float farGain = MIX_GetTrackGain(farTrack);

        EXPECT_LT(farGain, nearGain);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
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
    // "Explosion" has no wavebank reference, so Cue::active_ stays empty and Cue::ReconcileState()
    // never has anything to naturally finish (see P9-LIFECYCLE-001) -- this fire-and-forget cue
    // stays "in use" indefinitely here; Dispose (which clears the list immediately) is used as the
    // deterministic "stop" trigger instead of a real-time wait. See
    // IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes below for the real-instance case.
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_TRUE(bank.getIsInUseProperty());

    bank.Dispose();
    EXPECT_FALSE(bank.getIsInUseProperty());
}

// XA-7: IsInUse used to only check IsPlaying, so pausing a fire-and-forget cue made the bank
// falsely report itself as not in use -- FACT_STATE_INUSE (which IsInUse reflects) stays set
// while paused, it only clears once the cue is genuinely stopped.
TEST(SoundBankTest, IsInUseTrueWhilePausedNotJustWhilePlaying)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_TRUE(bank.getIsInUseProperty());

    Cue* cue = SoundBankTestAccess::LastFireAndForgetCue(bank);
    ASSERT_NE(cue, nullptr);
    cue->Pause();
    ASSERT_TRUE(cue->getIsPausedProperty());

    EXPECT_TRUE(bank.getIsInUseProperty());

    bank.Dispose();
}

// P12-BANK-001: IsInUse used to only look at fireAndForget_, so a cue the caller obtained via
// GetCue() (not PlayCue()) and is playing independently was invisible to it -- now activeCues_
// tracks both origins, matching WaveBank's already-correct broader registry.
TEST(SoundBankTest, IsInUseTrueForCueObtainedViaGetCueNotJustFireAndForget)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    std::unique_ptr<Cue> cue(bank.GetCue("Explosion"));
    ASSERT_FALSE(bank.getIsInUseProperty());

    cue->Play();
    EXPECT_TRUE(bank.getIsInUseProperty());

    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_FALSE(bank.getIsInUseProperty());
}

// P12-BANK-001: real FACT (FACTSoundBank_Destroy, FACT.c) force-stops every cue still using the
// bank when it's destroyed, including ones the caller obtained via GetCue() and is still holding
// -- previously SoundBank::Dispose() only cleared fireAndForget_, leaving a GetCue()-obtained cue
// playing (and its bank_ pointer dangling once the SoundBank itself was later destructed).
TEST(SoundBankTest, DisposeForceStopsCueObtainedViaGetCue)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    Cue* cue = bank.GetCue("Explosion");
    cue->Play();
    ASSERT_TRUE(cue->getIsPlayingProperty());
    ASSERT_FALSE(cue->getIsDisposedProperty());

    bank.Dispose();

    EXPECT_TRUE(cue->getIsDisposedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
    delete cue; // caller-owned; Cue::Dispose() is idempotent so the already-forced disposal is safe
}

// AUDIO-LIFECYCLE-001 (external audit, 2026-07-16): a cue obtained via GetCue() but never Play()'d
// was invisible to SoundBank::Dispose()'s force-stop cascade -- P12-BANK-001's own RegisterCue()
// call only ever ran from inside Cue::Play(), so this cue's constructor never registered it at
// all. Confirmed real by reading the source directly: with the bank disposed and the cue never
// having played, Play() would still run to completion afterward (dereferencing a bank_ that could
// be dangling once the SoundBank itself is later destructed), instead of throwing
// ObjectDisposedException like every other post-Dispose() call on this cue already does.
TEST(SoundBankTest, DisposeForceStopsNeverPlayedCueObtainedViaGetCue)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    Cue* cue = bank.GetCue("Explosion");
    ASSERT_FALSE(cue->getIsDisposedProperty());
    ASSERT_TRUE(cue->getIsPreparedProperty()); // never played

    bank.Dispose();

    EXPECT_TRUE(cue->getIsDisposedProperty());
    EXPECT_THROW(cue->Play(), System::ObjectDisposedException);
    delete cue; // caller-owned; Cue::Dispose() is idempotent so the already-forced disposal is safe
}

// AUDIO-LIFECYCLE-001: a cue that already played and then genuinely stopped (but that the caller
// hasn't disposed yet) must also stay reachable by the bank's force-stop cascade -- registration
// now lasts for this cue's entire C++ lifetime (constructor to Dispose()), not just while playing.
TEST(SoundBankTest, DisposeForceStopsAlreadyStoppedButUndisposedCueObtainedViaGetCue)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    Cue* cue = bank.GetCue("Explosion");
    cue->Play();
    cue->Stop(AudioStopOptions::Immediate);
    ASSERT_TRUE(cue->getIsStoppedProperty());
    ASSERT_FALSE(cue->getIsDisposedProperty());

    bank.Dispose();

    EXPECT_TRUE(cue->getIsDisposedProperty());
    delete cue;
}

// P9-LIFECYCLE-003/006: unlike "Explosion" above, "Apply3DCue" has a real (200-byte, ~1.13ms)
// WaveBank-backed instance, so its fire-and-forget cue actually finishes naturally -- IsInUse must
// become false soon afterward without any explicit Stop() or a second PlayCue() call to trigger
// the sweep (SoundBank::getIsInUseProperty() queries each cue's live, reconciled IsPlaying/
// IsPaused directly; it doesn't require the entry to have been swept from fireAndForget_ yet).
TEST(SoundBankTest, IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        (void)SharedApply3DWaveBank();
        SoundBank bank(&SharedEngine(), Apply3DXsbFixturePath());

        bank.PlayCue("Apply3DCue");

        if (!bank.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise real playback";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        EXPECT_FALSE(bank.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// Regression coverage for XA-2's XA-1 sibling bug: PlayCue()'s sweep used to remove any
// fire-and-forget cue older than 5 seconds regardless of whether it was still playing, cutting
// off any one-shot or music cue longer than that. It must now only remove entries that have
// actually finished playing (or exceeded the long safety-net timeout -- see the next test).
TEST(SoundBankTest, FireAndForgetCueSurvivesSweepPastOldFiveSecondThresholdWhileStillPlaying)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 1u);

    // Backdate well past the OLD buggy 5-second threshold, but nowhere near the new 5-minute
    // safety net -- exactly the scenario the bug got wrong: a long-but-still-playing cue.
    SoundBankTestAccess::BackdateLastFireAndForget(bank, std::chrono::seconds(30));

    bank.PlayCue("Explosion"); // triggers the sweep again
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 2u); // first entry must have survived

    bank.Dispose();
}

TEST(SoundBankTest, FireAndForgetCueIsForceSweptPastSafetyNetEvenIfStillPlaying)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 1u);

    // Past the 5-minute safety net (plans/plan_audio.md Fáze 7 D6): must be force-swept even though
    // Cue never reports itself as finished, so a caller that only ever PlayCue()s a
    // looping/very-long cue can't grow this list unbounded.
    SoundBankTestAccess::BackdateLastFireAndForget(bank, std::chrono::minutes(10));

    bank.PlayCue("Explosion");
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 1u); // old entry swept; only the new one remains

    bank.Dispose();
}

// XA-7: the sweep predicate used to check only IsPlaying, so pausing a fire-and-forget cue's
// category made the very next PlayCue() on this bank silently destroy it. A paused cue must
// survive the sweep exactly like a still-playing one, and remain genuinely resumable afterward
// -- not just "not yet garbage-collected".
TEST(SoundBankTest, PausedFireAndForgetCueSurvivesSweepAndCanStillBeResumed)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 1u);

    Cue* cue = SoundBankTestAccess::LastFireAndForgetCue(bank);
    ASSERT_NE(cue, nullptr);
    cue->Pause();
    ASSERT_TRUE(cue->getIsPausedProperty());
    // P9-LIFECYCLE-013: matches real FACT -- pausing never clears IsPlaying.
    ASSERT_TRUE(cue->getIsPlayingProperty());

    bank.PlayCue("Explosion"); // triggers the sweep again
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 2u); // paused entry must have survived

    cue->Resume();
    EXPECT_TRUE(cue->getIsPlayingProperty());

    bank.Dispose();
}

// ===================== GetTypeName =====================

TEST(SoundBankTest, GetTypeNameIsDottedXnaName)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_EQ(bank.GetTypeName(), "Microsoft.Xna.Framework.Audio.SoundBank");
}
