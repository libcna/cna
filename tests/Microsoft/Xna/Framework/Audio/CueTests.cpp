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

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor exposing which sound a variation table selected (via its category
    // index) without needing a real WaveBank/audio device to observe playback (see Cue.hpp).
    struct CueTestAccess
    {
        static uint16_t CategoryIndex(const Cue& cue) { return cue.categoryIdx_; }
    };
}

namespace
{
    using Microsoft::Xna::Framework::Audio::CueTestAccess;

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

    void AppendF32(std::vector<uint8_t>& buf, float v)
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

    // Minimal .xgs fixture with one category ("Default") and one variable ("Volume",
    // initial value 0.5) — gives Cue::GetVariable/SetVariable a real engine-known name.
    std::vector<uint8_t> BuildXgsFixtureBytes()
    {
        constexpr uint32_t headerSize        = 65;
        constexpr uint32_t categoryDataSize  = 10;
        constexpr uint32_t variableDataSize  = 13;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + variableDataSize;
        const std::string categoryName    = "Default";
        const uint32_t variableNameOffset = categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;
        const std::string variableName    = "Volume";

        std::vector<uint8_t> data;

        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, 1); // categoryCount
        AppendU16(data, 1); // variableCount
        AppendU16(data, 0); // blob1Count
        AppendU16(data, 0); // blob2Count
        AppendU16(data, 0); // rpcCount
        AppendU16(data, 0); // dspPresetCount
        AppendU16(data, 0); // dspParameterCount

        AppendU32(data, categoryOffset);
        AppendU32(data, variableOffset);
        AppendU32(data, 0); // blob1Offset
        AppendU32(data, 0); // categoryNameIndexOffset
        AppendU32(data, 0); // blob2Offset
        AppendU32(data, 0); // variableNameIndexOffset
        AppendU32(data, categoryNameOffset);
        AppendU32(data, variableNameOffset);

        // Category: instanceLimit, fadeInMS, fadeOutMS, maxInstanceBehavior(skip), parentIndex, volume, visibility
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0xFFFF);
        AppendU8(data, 0xFF);
        AppendU8(data, 0);

        // Variable: accessibility, initialValue, minValue, maxValue
        AppendU8(data, 0x03);
        AppendF32(data, 0.5f);
        AppendF32(data, 0.0f);
        AppendF32(data, 1.0f);

        AppendCStr(data, categoryName);
        AppendCStr(data, variableName);

        return data;
    }

    // Minimal .xsb with zero wavebanks/sounds and one simple cue named "Explosion".
    // Cue::Play() cannot resolve an actual sound (soundCount==0), so it takes the
    // "no sound found" branch and still transitions to State::Playing — enough to
    // exercise the full state machine without needing a WaveBank or audio device.
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

        AppendU8(data, 0);
        AppendU32(data, 0);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    // .xsb with 2 simple sounds (distinguished only by categoryIndex: 0 and 1) and one
    // complex cue ("Weighted") referencing a SOUND-type variation table with 2 entries whose
    // weight ranges are lopsided (weight 1 vs weight 99 out of a total of 100) -- regression
    // fixture for XA-3. Neither sound references a real wavebank, so Cue::Play() cannot spawn
    // a SoundEffectInstance, but it still resolves and stores the picked sound's category
    // index (categoryIdx_) before attempting to, which is enough to observe the pick without
    // a working audio device.
    std::vector<uint8_t> BuildXsbFixtureBytesWithWeightedVariation()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12; // simple sound: flags+cat+vol+pitch+prio+len+waveIdx+wbIdx

        const uint32_t soundOffset        = baseOffset;                 // 138
        const uint32_t sound0Code         = soundOffset;                 // 138
        const uint32_t sound1Code         = soundOffset + soundSize;     // 150
        const uint32_t variationOffset    = soundOffset + 2 * soundSize; // 162
        const uint32_t cueComplexOffset   = variationOffset + 20;        // 182 (table is 20 bytes)
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;       // 197 (entry is 15 bytes)
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;      // 203
        const std::string cueName         = "Weighted";

        std::vector<uint8_t> data;

        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0); // cueSimpleCount
        AppendU16(data, 1); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 0);  // wavebankCount
        AppendU16(data, 2); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, -1); // cueSimpleOffset
        AppendS32(data, static_cast<int32_t>(cueComplexOffset));
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, static_cast<int32_t>(variationOffset));
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "WeightedTestBank", bankNameSize);

        // Sound 0: categoryIndex=0 ("low weight" pick)
        AppendU8(data, 0);   // flags: simple, no RPC/DSP
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte (unused by this test)
        AppendU16(data, 0);  // pitchCents
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // Sound 1: categoryIndex=1 ("high weight" pick)
        AppendU8(data, 0);
        AppendU16(data, 1);
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);

        // Variation table: type=SOUND(1), 2 entries.
        const uint32_t entryCountAndFlags = 2u | (1u << 19); // entryCount=2, type=1 (SOUND)
        AppendU32(data, entryCountAndFlags);
        AppendU16(data, 0);   // unknown
        AppendU16(data, static_cast<uint16_t>(-1)); // variable (unused, not interactive)

        AppendU32(data, sound0Code); // entry 0: weight = 1-0 = 1
        AppendU8(data, 0);           // weightMin
        AppendU8(data, 1);           // weightMax

        AppendU32(data, sound1Code); // entry 1: weight = 100-1 = 99
        AppendU8(data, 1);           // weightMin
        AppendU8(data, 100);         // weightMax

        // Complex cue "Weighted": not single-sound, sbCode points at the variation table.
        AppendU8(data, 0);   // flags (CUE_FLAG_SINGLE_SOUND clear)
        AppendU32(data, variationOffset); // sbCode
        AppendU32(data, 0); // transitionOffset
        AppendU8(data, 0xFF); // instanceLimit
        AppendU16(data, 0); // fadeInMS
        AppendU16(data, 0); // fadeOutMS
        AppendU8(data, 0);  // maxInstanceBehavior

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0); // unknown

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

    const std::string& XgsFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_cue_test", "fixture.xgs", BuildXgsFixtureBytes());
        return path;
    }

    const std::string& XsbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_cue_test", "fixture.xsb", BuildXsbFixtureBytes());
        return path;
    }

    const std::string& XsbWeightedVariationFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_cue_test", "fixture_weighted.xsb", BuildXsbFixtureBytesWithWeightedVariation());
        return path;
    }

    // Both fixtures are parseable, unlike the "stub" AudioEngine used by
    // SoundBankTests.cpp/WaveBankTests.cpp, so "Volume" is a real, engine-known
    // global variable Cue::GetVariable/SetVariable can validate against.
    AudioEngine& SharedEngine()
    {
        static AudioEngine engine(XgsFixturePath());
        return engine;
    }

    SoundBank& SharedBank()
    {
        static SoundBank bank(&SharedEngine(), XsbFixturePath());
        return bank;
    }

    SoundBank& SharedWeightedVariationBank()
    {
        static SoundBank bank(&SharedEngine(), XsbWeightedVariationFixturePath());
        return bank;
    }

    std::unique_ptr<Cue> MakeCue()
    {
        return std::unique_ptr<Cue>(SharedBank().GetCue("Explosion"));
    }
}

// ===================== State properties (9) + Name =====================

TEST(CueTest, IsCreatedIsAlwaysFalse)
{
    // The constructor sets state_ straight to Prepared; nothing in Cue transitions
    // back to the Created state, so IsCreated is unreachable by design (matches
    // XACT's "created but not yet prepared" phase, which CNA's synchronous XSB
    // parsing skips entirely).
    auto cue = MakeCue();
    EXPECT_FALSE(cue->getIsCreatedProperty());
}

TEST(CueTest, IsPreparedTrueImmediatelyAfterCreation)
{
    auto cue = MakeCue();
    EXPECT_TRUE(cue->getIsPreparedProperty());
}

TEST(CueTest, IsPreparingIsAlwaysFalse)
{
    auto cue = MakeCue();
    EXPECT_FALSE(cue->getIsPreparingProperty());
}

TEST(CueTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    auto cue = MakeCue();
    EXPECT_FALSE(cue->getIsDisposedProperty());
    cue->Dispose();
    EXPECT_TRUE(cue->getIsDisposedProperty());
}

TEST(CueTest, IsPlayingTrueAfterPlay)
{
    auto cue = MakeCue();
    cue->Play();
    EXPECT_TRUE(cue->getIsPlayingProperty());
}

TEST(CueTest, IsPausedTrueAfterPause)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Pause();
    EXPECT_TRUE(cue->getIsPausedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(CueTest, IsStoppedTrueAfterStop)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(CueTest, IsStoppingIsAlwaysFalse)
{
    // StopInternal transitions straight from Playing to Stopped; there is no
    // intermediate release/fade phase modeled (SDL3_mixer stops synchronously).
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_FALSE(cue->getIsStoppingProperty());
}

TEST(CueTest, NameReturnsGivenName)
{
    auto cue = MakeCue();
    EXPECT_EQ(cue->getNameProperty(), "Explosion");
}

// ===================== Apply3D =====================

TEST(CueTest, Apply3DDoesNotThrowWhenNotDisposed)
{
    auto cue = MakeCue();
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_NO_THROW(cue->Apply3D(listener, emitter));
}

TEST(CueTest, Apply3DAfterDisposeThrowsObjectDisposed)
{
    auto cue = MakeCue();
    cue->Dispose();
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_THROW(cue->Apply3D(listener, emitter), System::ObjectDisposedException);
}

// ===================== GetVariable =====================

TEST(CueTest, GetVariableValidEngineVariableReturnsInitialValue)
{
    auto cue = MakeCue();
    EXPECT_FLOAT_EQ(cue->GetVariable("Volume"), 0.5f);
}

TEST(CueTest, GetVariableBuiltInVariableReturnsZeroByDefault)
{
    auto cue = MakeCue();
    EXPECT_FLOAT_EQ(cue->GetVariable("Distance"), 0.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("DopplerPitchScalar"), 0.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("OrientationAngle"), 0.0f);
}

TEST(CueTest, GetVariableAfterSetReturnsLocalOverride)
{
    auto cue = MakeCue();
    cue->SetVariable("Volume", 0.75f);
    EXPECT_FLOAT_EQ(cue->GetVariable("Volume"), 0.75f);
}

TEST(CueTest, GetVariableEmptyNameThrowsArgumentNull)
{
    auto cue = MakeCue();
    EXPECT_THROW((void)cue->GetVariable(""), System::ArgumentNullException);
}

TEST(CueTest, GetVariableNameOutsideKnownSetThrowsInvalidOperation)
{
    auto cue = MakeCue();
    EXPECT_THROW((void)cue->GetVariable("NoSuchVariable"), System::InvalidOperationException);
}

// ===================== SetVariable =====================

TEST(CueTest, SetVariableValidEngineVariableUpdatesValue)
{
    auto cue = MakeCue();
    cue->SetVariable("Volume", 0.1f);
    EXPECT_FLOAT_EQ(cue->GetVariable("Volume"), 0.1f);
}

TEST(CueTest, SetVariableBuiltInVariableDoesNotThrow)
{
    auto cue = MakeCue();
    EXPECT_NO_THROW(cue->SetVariable("OrientationAngle", 45.0f));
    EXPECT_FLOAT_EQ(cue->GetVariable("OrientationAngle"), 45.0f);
}

TEST(CueTest, SetVariableEmptyNameThrowsArgumentNull)
{
    auto cue = MakeCue();
    EXPECT_THROW(cue->SetVariable("", 1.0f), System::ArgumentNullException);
}

TEST(CueTest, SetVariableNameOutsideKnownSetThrowsInvalidOperation)
{
    auto cue = MakeCue();
    EXPECT_THROW(cue->SetVariable("NoSuchVariable", 1.0f), System::InvalidOperationException);
}

// ===================== Play / Pause / Resume / Stop =====================

TEST(CueTest, PlayTransitionsToPlaying)
{
    auto cue = MakeCue();
    cue->Play();
    EXPECT_TRUE(cue->getIsPlayingProperty());
}

TEST(CueTest, PlayAfterDisposeThrowsObjectDisposed)
{
    auto cue = MakeCue();
    cue->Dispose();
    EXPECT_THROW(cue->Play(), System::ObjectDisposedException);
}

TEST(CueTest, PauseThenResumeReturnsToPlaying)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Pause();
    ASSERT_TRUE(cue->getIsPausedProperty());
    cue->Resume();
    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_FALSE(cue->getIsPausedProperty());
}

TEST(CueTest, StopAsAuthoredTransitionsToStopped)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(CueTest, StopImmediateTransitionsToStopped)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

// ===================== Dispose =====================

TEST(CueTest, DisposeIsIdempotent)
{
    auto cue = MakeCue();
    cue->Dispose();
    EXPECT_NO_THROW(cue->Dispose());
    EXPECT_TRUE(cue->getIsDisposedProperty());
}

TEST(CueTest, DisposeRaisesDisposingEvent)
{
    auto cue = MakeCue();
    bool raised = false;
    cue->Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    cue->Dispose();
    EXPECT_TRUE(raised);
}

// ===================== GetTypeName =====================

TEST(CueTest, GetTypeNameIsDottedXnaName)
{
    auto cue = MakeCue();
    EXPECT_EQ(cue->GetTypeName(), "Microsoft.Xna.Framework.Audio.Cue");
}

// ===================== Variation selection (XA-3) =====================

TEST(CueTest, PlayWeightedVariationFavorsHigherWeightEntryStatistically)
{
    auto cue = std::unique_ptr<Cue>(SharedWeightedVariationBank().GetCue("Weighted"));

    constexpr int kIterations = 200;
    int highWeightPicks = 0;
    for (int i = 0; i < kIterations; ++i)
    {
        cue->Play();
        if (CueTestAccess::CategoryIndex(*cue) == 1)
            ++highWeightPicks;
    }

    // Entry 1 carries weight 99 of a total 100 -- it should be picked ~99% of the time.
    // Uniform (unweighted) selection between the two entries would land close to 50%, so an
    // 80% threshold cleanly distinguishes weighted selection from the old uniform pick.
    EXPECT_GT(highWeightPicks, kIterations * 0.8);
}
