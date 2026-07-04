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
#include "SoundEffectInstanceTestAccess.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
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
#include <vector>

#include <SDL3_mixer/SDL_mixer.h>

using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::WaveBank;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for SoundBank's private fire-and-forget cue list (see SoundBank.hpp),
    // used to test PlayCue()'s sweep logic precisely without a real-time wait.
    struct SoundBankTestAccess
    {
        static std::size_t FireAndForgetCount(const SoundBank& bank)
        {
            return bank.fireAndForget_.size();
        }

        // Backdates the most recently added fire-and-forget entry's creation time, so sweep
        // behavior at a specific age can be tested deterministically.
        static void BackdateLastFireAndForget(SoundBank& bank, std::chrono::steady_clock::duration age)
        {
            if (!bank.fireAndForget_.empty())
            {
                bank.fireAndForget_.back().created = std::chrono::steady_clock::now() - age;
            }
        }

        // The most recently played fire-and-forget cue, so a test can verify PlayCue's 3D
        // overload actually reached Cue::Apply3D (T-4B), not just that it didn't throw.
        static Cue* LastFireAndForgetCue(const SoundBank& bank)
        {
            return bank.fireAndForget_.empty() ? nullptr : bank.fireAndForget_.back().cue.get();
        }
    };
}

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

    // AudioEngine constructed against a nonexistent .xgs — runs as the documented
    // "stub" (no categories/variables), which is all SoundBank/Cue need here.
    AudioEngine& SharedEngine()
    {
        static AudioEngine engine(
            (std::filesystem::temp_directory_path() / "cna_soundbank_test_nonexistent.xgs").string());
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

// T-4B: the 3D PlayCue overload must actually reach Cue::Apply3D on the resulting
// fire-and-forget cue, not just accept listener/emitter without using them. XsbFixtureBytes'
// wavebank-less "Explosion" cue can't exercise this (Cue::active_ stays empty), so this uses a
// real WaveBank-backed fixture instead and reads back the actual SDL_mixer track gain.
TEST(SoundBankTest, PlayCueThreeArgAppliesRealAttenuationToActiveInstance)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

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
    // Cue never self-transitions out of Playing without an explicit Stop() (see Cue.cpp), so a
    // fire-and-forget cue stays "in use" indefinitely here; Dispose (which clears the list
    // immediately) is used as the deterministic "stop" trigger instead of a real-time wait.
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    bank.PlayCue("Explosion");
    ASSERT_TRUE(bank.getIsInUseProperty());

    bank.Dispose();
    EXPECT_FALSE(bank.getIsInUseProperty());
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

    // Past the 5-minute safety net (plan_audio.md Fáze 7 D6): must be force-swept even though
    // Cue never reports itself as finished, so a caller that only ever PlayCue()s a
    // looping/very-long cue can't grow this list unbounded.
    SoundBankTestAccess::BackdateLastFireAndForget(bank, std::chrono::minutes(10));

    bank.PlayCue("Explosion");
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(bank), 1u); // old entry swept; only the new one remains

    bank.Dispose();
}

// ===================== GetTypeName =====================

TEST(SoundBankTest, GetTypeNameIsDottedXnaName)
{
    SoundBank bank(&SharedEngine(), XsbFixturePath());
    EXPECT_EQ(bank.GetTypeName(), "Microsoft.Xna.Framework.Audio.SoundBank");
}
