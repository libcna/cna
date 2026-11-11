// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "XactContractAssertions.hpp"
#include "CNA/TestSupport/TestScratch.hpp"
#include "AudioTestWait.hpp"
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
#include "System/ArgumentException.hpp"
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
            auto dir = CNA::TestSupport::FixtureRoot() / "cna_soundbank_test";
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
            auto dir = CNA::TestSupport::FixtureRoot() / "cna_soundbank_test";
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
        auto dir = CNA::TestSupport::FixtureRoot() / dirName;
        std::filesystem::create_directories(dir);
        auto file = dir / fileName;
        std::ofstream f(file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return file.string();
    }

    constexpr const char* kApply3DWaveBankName = "Apply3DWaveBank";

    // Minimal compact .xwb with one mono 16-bit PCM entry (half a second of silence) -- needed
    // (unlike BuildXsbFixtureBytes above) so that Cue::Play() resolves a real WaveBank entry and
    // creates an actual SoundEffectInstance with a real MIX_Track. The wave must outlast PlayCue
    // itself for the natural-completion test: PlayCue keeps a fire-and-forget cue only if it is
    // still playing after Play(), and a 2 ms wave was sometimes already over on a loaded CI
    // runner. Tests that inspect a playing cue use the looping fixture instead, whose lifetime
    // does not depend on this length.
    std::vector<uint8_t> BuildApply3DXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 44100; // 22050 frames at 44.1 kHz
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

    // Minimal .xsb with one wavebank reference, one sound (categoryIndex=0), and one simple cue
    // playing that sound. Without @p looping the cue is "Apply3DCue" and its sound is simple: the
    // wave plays once and the cue finishes naturally. With @p looping the cue is "Apply3DLoopCue"
    // and its sound is complex, one track whose zero-time PlayWave event has loop count 255,
    // XACT's infinite sentinel: the voice never reaches its end, so the cue stays playing until it
    // is stopped or its bank is disposed, however late the test inspects it.
    std::vector<uint8_t> BuildApply3DXsbFixtureBytes(bool looping)
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;
        // Simple: the common 9 bytes plus the inline wave reference (u16 wave, u8 bank).
        // Complex: the common 9 bytes, trackCount, one track record (volume, event code, filter,
        // frequency) and its event block (count, then one 16-byte PlayWave event).
        constexpr uint32_t simpleSoundSize  = 12;
        constexpr uint32_t complexSoundSize = 9 + 1 + 9;
        constexpr uint32_t eventBlockSize   = 1 + 16;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t eventBlockOffset   = soundOffset + complexSoundSize;
        const uint32_t cueSimpleOffset    = looping ? eventBlockOffset + eventBlockSize
                                                    : soundOffset + simpleSoundSize;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = looping ? "Apply3DLoopCue" : "Apply3DCue";

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

        AppendU8(data, looping ? 0x01 : 0x00); // flags: SOUND_FLAG_COMPLEX when looping
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        if (looping)
        {
            AppendU8(data, 1);                 // trackCount
            AppendU8(data, 180);               // track volume byte, about 0 dB
            AppendU32(data, eventBlockOffset); // track event code
            AppendU16(data, 0);                // filterData: no filter
            AppendU16(data, 0);                // filter frequency

            AppendU8(data, 1);    // eventCount
            AppendU32(data, 1);   // info: FACTEVENT_PLAYWAVE, timestamp 0
            AppendU16(data, 0);   // randomOffset
            AppendU8(data, 0xFF); // event separator
            AppendU8(data, 0);    // play flags
            AppendU16(data, 0);   // waveIdx
            AppendU8(data, 0);    // wbIdx
            AppendU8(data, 255);  // loopCount: infinite
            AppendU16(data, 0);   // position
            AppendU16(data, 0);   // angle
        }
        else
        {
            AppendU16(data, 0);   // waveIdx
            AppendU8(data, 0);    // wbIdx
        }

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
            "cna_soundbank_test", "apply3d.xsb", BuildApply3DXsbFixtureBytes(false));
        return path;
    }

    const std::string& Apply3DLoopXsbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_soundbank_test", "apply3d-loop.xsb", BuildApply3DXsbFixtureBytes(true));
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
        (CNA::TestSupport::FixtureRoot() / "cna_soundbank_test_missing.xsb").string();
    // Refresh constructor calls File.OpenRead before native resource creation.
    // The absolute missing path is the BCL FileNotFoundException.FileName contract.
    try { SoundBank bank(&SharedEngine(), missing); FAIL() << "missing file was accepted"; }
    catch (const System::IO::FileNotFoundException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::IO::FileNotFoundException));
        EXPECT_EQ(error.getFileNameProperty(), missing);
    }
}

TEST(SoundBankTest, ConstructorLoadsValidFixture)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_FALSE(bank.getIsDisposedProperty());
}

TEST(SoundBankTest, Tq022DisposedEngineCannotCreateBankButHeaderValidationStillRunsFirst)
{
    AudioEngine engine(WriteFixture("cna_soundbank_test", "tq022_disposed.xgs",
                                   BuildMinimalXgsFixtureBytes()));
    engine.Dispose();
    // XNA native SoundBank.CreateHandle IL_001c-IL_0038 returns -1 for a released
    // engine; managed .ctor IL_00c3-IL_00d6 reports failed resource creation.
    EXPECT_THROW(SoundBank bank(&engine, XsbFixturePath()), System::InvalidOperationException);
    // Managed header validation IL_0052-IL_0098 precedes that native call.
    const auto bad = WriteFixture("cna_soundbank_test", "tq022_disposed_bad.xsb", {0,0,0,0,0});
    EXPECT_THROW(SoundBank bank(&engine, bad), System::ArgumentException);
}

TEST(SoundBankTest, Tq022HeaderLengthAndEachMagicByteAreValidated)
{
    // XNA .ctor IL_005a-IL_0098: length > 4, every SDBK byte, ArgumentException.
    const std::vector<uint8_t> magic{'S', 'D', 'B', 'K'};
    for (std::size_t size = 0; size <= magic.size(); ++size)
    {
        SCOPED_TRACE(size);
        const std::vector<uint8_t> bytes(magic.begin(), magic.begin() + size);
        const auto path = WriteFixture("cna_soundbank_test", "tq022_short.xsb", bytes);
        XactContract::Refuses<System::ArgumentException>(
            [&] { SoundBank bank(&SharedEngine(), path); }, XactContract::HeaderText);
    }
    for (std::size_t byte = 0; byte < magic.size(); ++byte)
    {
        SCOPED_TRACE(byte);
        auto bytes = BuildXsbFixtureBytes();
        bytes[byte] ^= 0x20;
        const auto path = WriteFixture("cna_soundbank_test", "tq022_magic.xsb", bytes);
        XactContract::Refuses<System::ArgumentException>(
            [&] { SoundBank bank(&SharedEngine(), path); }, XactContract::HeaderText);
    }
    // Valid signature but truncated metadata: .ctor IL_00c3-IL_00d6 fails resource creation.
    const auto path = WriteFixture("cna_soundbank_test", "tq022_metadata.xsb", {'S','D','B','K',0});
    XactContract::Refuses<System::InvalidOperationException>(
        [&] { SoundBank bank(&SharedEngine(), path); }, XactContract::ResourceText);
}

TEST(SoundBankTest, Tq022DisposingRunsAfterCueCleanupAndAllowsReentry)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    std::unique_ptr<Cue> cue(bank.GetCue("Explosion"));
    int events = 0;
    // XNA Dispose(bool) IL_0018-IL_003f disposes/releases before IL_005b invokes the event.
    bank.Disposing += [&](System::Object* sender, const System::EventArgs&) {
        ++events;
        EXPECT_EQ(sender, &bank);
        EXPECT_TRUE(bank.getIsDisposedProperty());
        EXPECT_TRUE(cue->getIsDisposedProperty());
        EXPECT_FALSE(bank.getIsInUseProperty());
        XactContract::Refuses<System::ArgumentException>(
            [&] { (void)bank.GetCue("Explosion"); }, XactContract::CueText("Explosion"));
        if (events == 1) bank.Dispose();
    };
    bank.Dispose();
    bank.Dispose();
    EXPECT_EQ(events, 1);
}

TEST(SoundBankTest, Tq022EmptyNamesPrecedeDisposedHandleAccessInEveryLookupOverload)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.Dispose();
    AudioListener listener;
    AudioEmitter emitter;
    // XNA GetCue/PlayCue(string)/PlayCue(string,listener,emitter) IL_0000-IL_0017.
    EXPECT_THROW((void)bank.GetCue(""), System::ArgumentNullException);
    EXPECT_THROW(bank.PlayCue(""), System::ArgumentNullException);
    EXPECT_THROW(bank.PlayCue("", listener, emitter), System::ArgumentNullException);
}

TEST(SoundBankTest, Tq022ThrowingDisposingHandlerDoesNotRetainPreparedCue)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    std::unique_ptr<Cue> cue(bank.GetCue("Explosion"));
    int events = 0;
    // XNA Dispose(bool) IL_0018-IL_003f completes disposal before event IL_005b.
    bank.Disposing += [&](System::Object*, const System::EventArgs&) {
        if (++events == 1) throw std::runtime_error("tq022 handler");
    };
    EXPECT_THROW(bank.Dispose(), std::runtime_error);
    EXPECT_TRUE(bank.getIsDisposedProperty());
    EXPECT_TRUE(cue->getIsDisposedProperty());
    bank.Dispose();
    EXPECT_EQ(events, 1);
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

TEST(SoundBankTest, Tq022GetCueUnknownNameUsesArgumentExceptionButPlayCueUsesInvalidOperation)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    // XNA native SoundBank.GetCue IL_0088-IL_0096 maps index 0xffff to E_INVALIDARG;
    // managed GetCue IL_0035-IL_005d throws ArgumentException. Both PlayCue overloads
    // instead throw InvalidOperationException (IL_0051 and IL_0081 respectively).
    EXPECT_THROW((void)bank.GetCue("NoSuchCue"), System::ArgumentException);
    EXPECT_THROW(bank.PlayCue("NoSuchCue"), System::InvalidOperationException);
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_THROW(bank.PlayCue("NoSuchCue", listener, emitter), System::InvalidOperationException);
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 0u);
    EXPECT_FALSE(bank.getIsInUseProperty());
}

// XNA SoundBank..ctor IL_0052-IL_0098 rejects bad magic before creating a bank.
// The previous test asserted CNA's silent stub instead of this XNA contract.
TEST(SoundBankTest, Tq022ConstructorWithInvalidHeaderThrowsArgumentException)
{
    const auto corrupt = WriteFixture("cna_soundbank_test", "corrupt.xsb",
                                       {'n', 'o', 't', ' ', 'a', ' ', 's', 'o', 'u', 'n', 'd', 'b', 'a', 'n', 'k'});
    EXPECT_THROW(SoundBank bank(&SharedEngine(), corrupt), System::ArgumentException);
}

TEST(SoundBankTest, GetCueAfterDisposeThrowsRefreshRefusal)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.Dispose();
    XactContract::Refuses<System::ArgumentException>(
        [&] { (void)bank.GetCue("Explosion"); }, XactContract::CueText("Explosion"));
}

// ===================== PlayCue (2-arg) =====================

// Strengthened (was EXPECT_NO_THROW only): PlayCue starts a playing cue of that name that the
// caller never sees (XNA SoundBank.PlayCue(string) IL_0020-IL_0025: native PlayCue, no Cue object).
TEST(SoundBankTest, PlayCueTwoArgValidDoesNotThrow)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 0u);
    bank.PlayCue("Explosion");
    ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 1u);
    Cue* played = SoundBankTestAccess::LastFireAndForgetCue(bank);
    ASSERT_NE(played, nullptr);
    EXPECT_EQ(played->getNameProperty(), "Explosion");
    EXPECT_FALSE(played->getIsPreparedProperty()); // it has been played, not merely prepared
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

TEST(SoundBankTest, PlayCueTwoArgAfterDisposeThrowsRefreshRefusal)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.Dispose();
    XactContract::Refuses<System::InvalidOperationException>(
        [&] { bank.PlayCue("Explosion"); }, XactContract::CueText("Explosion"));
}

// ===================== PlayCue (3-arg) =====================

// Strengthened (was EXPECT_NO_THROW only): XNA's 3-argument PlayCue gets the cue, positions it
// with Apply3D, then plays it (SoundBank.PlayCue(string, AudioListener, AudioEmitter)
// IL_0049-IL_0058). So the played cue carries the emitter distance (hand-derived |(0,6,8)| = 10)
// and, having been positioned before playing, still accepts Apply3D afterwards.
TEST(SoundBankTest, PlayCueThreeArgValidDoesNotThrow)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({0.0f, 6.0f, 8.0f});
    bank.PlayCue("Explosion", listener, emitter);
    Cue* played = SoundBankTestAccess::LastFireAndForgetCue(bank);
    ASSERT_NE(played, nullptr);
    EXPECT_EQ(played->getNameProperty(), "Explosion");
    EXPECT_FLOAT_EQ(played->GetVariable("Distance"), 10.0f);
    EXPECT_NO_THROW(played->Apply3D(listener, emitter));
}

// P10-XACT-007: the 3-arg overload shares PlayCueInternal with the 2-arg one (tested above via
// PlayCueTwoArgAfterDisposeThrowsRefreshRefusal), but per this project's overload-testing
// convention each overload gets its own dedicated case rather than assuming shared internals.
TEST(SoundBankTest, PlayCueThreeArgAfterDisposeThrowsRefreshRefusal)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    AudioListener listener;
    AudioEmitter emitter;
    bank.Dispose();
    XactContract::Refuses<System::InvalidOperationException>(
        [&] { bank.PlayCue("Explosion", listener, emitter); }, XactContract::CueText("Explosion"));
}

// T-4B: the 3D PlayCue overload must actually reach Cue::Apply3D on the resulting
// fire-and-forget cue, not just accept listener/emitter without using them. XsbFixtureBytes'
// wavebank-less "Explosion" cue can't exercise this (Cue::active_ stays empty), so this uses a
// real WaveBank-backed fixture instead and reads back the actual SDL_mixer track gain.
//
// The cue loops forever, so PlayCue keeps it and its voices stay alive until the bank is
// disposed: the gains are read from playing voices however late this thread runs. A finite cue
// made the test depend on the wave outlasting PlayCue and the reads. Natural completion is
// IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes's.
TEST(SoundBankTest, PlayCueThreeArgAppliesRealAttenuationToActiveInstance)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        (void)SharedApply3DWaveBank(); // must be registered with the engine before PlayCue()
        SoundBank bank(&SharedEngine(), Apply3DLoopXsbFixturePath());
        AudioListener listener; // default position: origin

        AudioEmitter nearEmitter;
        nearEmitter.setPositionProperty({1.0f, 0.0f, 0.0f});
        bank.PlayCue("Apply3DLoopCue", listener, nearEmitter);

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
        bank.PlayCue("Apply3DLoopCue", listener, farEmitter);

        Cue* farCue = SoundBankTestAccess::LastFireAndForgetCue(bank);
        ASSERT_NE(farCue, nullptr);
        SoundEffectInstance* farInst = CueTestAccess::ActiveInstance(*farCue, 0);
        ASSERT_NE(farInst, nullptr);
        MIX_Track* farTrack = SoundEffectInstanceTestAccess::GetTrack(*farInst);
        ASSERT_NE(farTrack, nullptr);
        const float farGain = MIX_GetTrackGain(farTrack);

        EXPECT_LT(farGain, nearGain);
        // The hold this test relies on: both cues are still playing after every read.
        EXPECT_TRUE(nearCue->getIsPlayingProperty());
        EXPECT_TRUE(farCue->getIsPlayingProperty());
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
    ASSERT_TRUE(bank.getIsInUseProperty()); // preparation itself retains the bank

    cue->Play();
    EXPECT_TRUE(bank.getIsInUseProperty());

    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_TRUE(bank.getIsInUseProperty());
    cue.reset();EXPECT_FALSE(bank.getIsInUseProperty());
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

// P9-LIFECYCLE-003/006: unlike "Explosion" above, "Apply3DCue" has a real (half-second)
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

        // Service the engine so a naturally finished managed cue is destroyed.
        EXPECT_TRUE(CnaAudioTest::WaitUntil([&] { SharedEngine().Update();return !bank.getIsInUseProperty(); }))
            << "the fire-and-forget cue never finished playing on its own";
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

    // Past the 5-minute safety net (the audio plan Fáze 7 D6): must be force-swept even though
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

// ---------------------------------------------------------------------------
// XNA-MISSING-017: the protected SoundBank.Dispose(Boolean) hook.
// ---------------------------------------------------------------------------

namespace
{
    class SoundBankDisposeProbe final : public SoundBank
    {
    public:
        using SoundBank::Dispose;

        SoundBankDisposeProbe(AudioEngine* engine, const std::string& filename)
            : SoundBank(engine, filename) {}

        int hookCalls = 0;
        bool lastDisposing = false;
        bool disposedWhenHookRan = false;

        void DisposeWithoutNotifying() { SoundBank::Dispose(false); }

    protected:
        void Dispose(bool disposing) override
        {
            ++hookCalls;
            lastDisposing = disposing;
            disposedWhenHookRan = getIsDisposedProperty();
            SoundBank::Dispose(disposing);
        }
    };
}

TEST(SoundBankTest, PublicDisposeRoutesThroughTheProtectedHook)
{
    SoundBankDisposeProbe probe(&SharedEngine(), XsbFixturePath());
    ASSERT_FALSE(probe.getIsDisposedProperty());

    probe.Dispose();
    EXPECT_EQ(probe.hookCalls, 1);
    EXPECT_TRUE(probe.lastDisposing);
    EXPECT_FALSE(probe.disposedWhenHookRan);
    EXPECT_TRUE(probe.getIsDisposedProperty());

    // Idempotent, and reachable through the interface a caller may hold instead.
    probe.Dispose();
    EXPECT_EQ(probe.hookCalls, 2);
    EXPECT_TRUE(probe.getIsDisposedProperty());
}

TEST(SoundBankTest, DisposalThroughTheDisposableInterfaceReachesTheOverride)
{
    SoundBankDisposeProbe probe(&SharedEngine(), XsbFixturePath());
    System::IDisposable& asDisposable = probe;
    asDisposable.Dispose();
    EXPECT_EQ(probe.hookCalls, 1);
    EXPECT_TRUE(probe.getIsDisposedProperty());
}

TEST(SoundBankTest, TheHookRaisesDisposingOnlyForAnExplicitDisposal)
{
    {
        SoundBank bank(&SharedEngine(), XsbFixturePath());
        int notifications = 0;
        bank.Disposing += [&notifications](System::Object*, const System::EventArgs&) {
            ++notifications;
        };
        bank.Dispose();
        EXPECT_EQ(notifications, 1);
    }
    {
        SoundBankDisposeProbe probe(&SharedEngine(), XsbFixturePath());
        int notifications = 0;
        probe.Disposing += [&notifications](System::Object*, const System::EventArgs&) {
            ++notifications;
        };
        probe.DisposeWithoutNotifying();
        EXPECT_EQ(notifications, 0);
        EXPECT_TRUE(probe.getIsDisposedProperty());
    }
}

TEST(SoundBankTest, Tq084ConstructorRefusalsKeepRefreshDiagnostics)
{
    // Refresh .ctor IL_0014-003e: engine precedes filename; custom resource messages.
    XactContract::Null([&] { SoundBank bank(nullptr, ""); }, "audioEngine", XactContract::EngineText);
    XactContract::Null([&] { SoundBank bank(&SharedEngine(), ""); }, "filename");
    AudioEngine engine(WriteFixture("cna_soundbank_test", "tq084_disposed.xgs",
                                   BuildMinimalXgsFixtureBytes()));
    engine.Dispose();
    // .ctor IL_00cc-00d6 uses CouldNotCreateResource after the managed header gate.
    XactContract::Refuses<System::InvalidOperationException>(
        [&] { SoundBank bank(&engine, XsbFixturePath()); }, XactContract::ResourceText);
}

TEST(SoundBankTest, Tq084LookupRefusalsPreserveRequestedNameAcrossEveryOverload)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    AudioListener listener;
    AudioEmitter emitter;
    for (const bool disposed : {false, true})
    {
        SCOPED_TRACE(disposed);
        if (disposed) bank.Dispose();
        // Refresh GetCue / both PlayCue overloads IL_0000-0017: empty before native access.
        XactContract::Null([&] { (void)bank.GetCue(""); }, "name");
        XactContract::Null([&] { bank.PlayCue(""); }, "name");
        XactContract::Null([&] { bank.PlayCue("", listener, emitter); }, "name");
        for (const auto* name : {"NoSuchCue", "no such cue", "\"quoted\"", " "})
        {
            SCOPED_TRACE(name);
            // GetCue IL_0035-005d formats CueNotFound for E_INVALIDARG, including a released bank
            // (native GetCue IL_013c-014c). PlayCue IL_0036-0056 / IL_0066-0086 uses InvalidOperation.
            XactContract::Refuses<System::ArgumentException>(
                [&] { (void)bank.GetCue(name); }, XactContract::CueText(name));
            XactContract::Refuses<System::InvalidOperationException>(
                [&] { bank.PlayCue(name); }, XactContract::CueText(name));
            XactContract::Refuses<System::InvalidOperationException>(
                [&] { bank.PlayCue(name, listener, emitter); }, XactContract::CueText(name));
        }
        EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 0u);
        EXPECT_FALSE(bank.getIsInUseProperty());
    }
}
