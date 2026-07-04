// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CueTestAccess.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::CueTestAccess;
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

    void AppendCStr(std::vector<uint8_t>& buf, const std::string& s)
    {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(0);
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

    void AppendCategory(std::vector<uint8_t>& buf)
    {
        AppendU8(buf, 0xFF);   // instanceLimit
        AppendU16(buf, 0);     // fadeInMS
        AppendU16(buf, 0);     // fadeOutMS
        AppendU8(buf, 0);      // maxInstanceBehavior (skip)
        AppendU16(buf, 0xFFFF);// parentIndex (none)
        AppendU8(buf, 0xFF);   // volume byte
        AppendU8(buf, 0);      // visibility
    }

    // Minimal .xgs fixture with two categories ("Default", "Combat") and no variables.
    std::vector<uint8_t> BuildXgsFixtureBytes()
    {
        constexpr uint32_t headerSize       = 65;
        constexpr uint32_t categoryDataSize = 10;
        constexpr uint32_t categoryCount    = 2;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryCount * categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset; // variableCount == 0, nothing stored there
        const std::string name0 = "Default";
        const std::string name1 = "Combat";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(name0.size()) + 1 +
            static_cast<uint32_t>(name1.size()) + 1;

        std::vector<uint8_t> data;

        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, categoryCount); // categoryCount
        AppendU16(data, 0); // variableCount
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

        AppendCategory(data); // "Default"
        AppendCategory(data); // "Combat"

        AppendCStr(data, name0);
        AppendCStr(data, name1);

        return data;
    }

    AudioEngine& SharedEngine()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_audio_category_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xgs";
            const auto bytes = BuildXgsFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        static AudioEngine engine(path);
        return engine;
    }

    // Minimal .xsb with one simple cue ("TestCue") whose sound has categoryIndex=0 ("Default",
    // the first category in BuildXgsFixtureBytes). No wavebank is needed: Cue::Play() sets
    // categoryIdx_/state_ and registers with the engine's activeCues list regardless of whether
    // any wave reference actually resolves to a real WaveBank -- enough to exercise
    // AudioCategory's routing to a real, currently-playing Cue (XA-5).
    std::vector<uint8_t> BuildXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12; // simple sound: flags+cat+vol+pitch+prio+len+waveIdx+wbIdx

        const uint32_t soundOffset       = baseOffset;             // 138
        const uint32_t cueSimpleOffset   = soundOffset + soundSize; // 150
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;    // 155
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6; // 161
        const std::string cueName = "TestCue";

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
        AppendU16(data, 1); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, static_cast<int32_t>(cueSimpleOffset));
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "TestSoundBank", bankNameSize);

        // Sound: simple, categoryIndex=0 ("Default").
        AppendU8(data, 0);   // flags
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte (unused by this test)
        AppendU16(data, 0);  // pitchCents
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // Simple cue "TestCue", pointing at the sound above.
        AppendU8(data, 0);   // flags
        AppendU32(data, soundOffset); // sbCode

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0); // unknown

        AppendCStr(data, cueName);

        return data;
    }

    const std::string& XsbFixturePath()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_audio_category_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xsb";
            const auto bytes = BuildXsbFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        return path;
    }

    SoundBank& SharedBank()
    {
        static SoundBank bank(&SharedEngine(), XsbFixturePath());
        return bank;
    }

    constexpr const char* kVolWaveBankName = "VolWaveBank";

    // Minimal compact .xwb with one mono 16-bit PCM entry (200 bytes of silence) -- reused
    // pattern from WaveBankTests.cpp. Needed (unlike BuildXsbFixtureBytes above) so that
    // Cue::Play() resolves a real WaveBank entry and creates an actual SoundEffectInstance in
    // Cue::active_ -- required to verify AudioCategory::SetVolume's effect on already-playing
    // instances (T-4D), which BuildXsbFixtureBytes's wavebank-less cue can't exercise.
    std::vector<uint8_t> BuildVolXwbFixtureBytes()
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
        AppendPadded(data, kVolWaveBankName, 64);
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

    // Minimal .xsb with one wavebank reference, one simple sound (categoryIndex=0, "Default"),
    // and one simple cue "VolCue" playing that sound.
    std::vector<uint8_t> BuildVolXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "VolCue";

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

        AppendPadded(data, "VolSoundBank", bankNameSize);
        AppendPadded(data, kVolWaveBankName, 64);

        // Sound: simple, categoryIndex=0 ("Default").
        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        // Simple cue "VolCue", pointing at the sound above.
        AppendU8(data, 0);
        AppendU32(data, soundOffset);

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

    const std::string& VolXwbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_audio_category_test", "volfixture.xwb", BuildVolXwbFixtureBytes());
        return path;
    }

    const std::string& VolXsbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_audio_category_test", "volfixture.xsb", BuildVolXsbFixtureBytes());
        return path;
    }

    WaveBank& SharedVolWaveBank()
    {
        static WaveBank wb(&SharedEngine(), VolXwbFixturePath());
        return wb;
    }

    SoundBank& SharedVolBank()
    {
        (void)SharedVolWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), VolXsbFixturePath());
        return bank;
    }
}

// ===================== Name =====================

TEST(AudioCategoryTest, NameReturnsGivenName)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_EQ(cat.getNameProperty(), "Default");
}

// ===================== Pause / Resume / SetVolume / Stop =====================

TEST(AudioCategoryTest, PauseDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Pause());
}

TEST(AudioCategoryTest, ResumeDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Resume());
}

TEST(AudioCategoryTest, SetVolumeDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.SetVolume(0.5f));
}

TEST(AudioCategoryTest, StopAsAuthoredDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Stop(AudioStopOptions::AsAuthored));
}

TEST(AudioCategoryTest, StopImmediateDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Stop(AudioStopOptions::Immediate));
}

// XA-5: AudioCategory.hpp documents that Pause/Resume/Stop "route to every currently active
// Cue... and have a real, immediate effect on playback" -- verify that against a real playing
// Cue in the category, not just EXPECT_NO_THROW with no active cue at all.
TEST(AudioCategoryTest, PauseResumeStopRouteToRealActiveCueInCategory)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    std::unique_ptr<Cue> cue(SharedBank().GetCue("TestCue"));

    cue->Play();
    ASSERT_TRUE(cue->getIsPlayingProperty());

    cat.Pause();
    EXPECT_TRUE(cue->getIsPausedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());

    cat.Resume();
    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_FALSE(cue->getIsPausedProperty());

    EXPECT_NO_THROW(cat.SetVolume(0.5f));

    cat.Stop(AudioStopOptions::Immediate);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

// P9-CATEGORY-001/002: StopCategoryInternal used to iterate AudioEngine::activeCues directly
// while Cue::Stop() cascades into UnregisterCue(), which erases from that same vector --
// mutating a vector while range-for-iterating it invalidates the loop's cached end() iterator.
// With 3+ cues in one category this reliably skips stopping at least one of them (traced by hand
// through std::remove's element-shift pattern: the skipped cue is always the one whose slot gets
// backfilled from beyond the range-for's stale cached end()). Needs 3, not 2, cues to reliably
// reproduce -- with exactly 2, the single leftover stale slot happens to still hold the right
// pointer by accident of std::remove's shift, masking the bug.
TEST(AudioCategoryTest, StopStopsAllActiveCuesInCategoryNotJustSomeOfThem)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    std::unique_ptr<Cue> cueA(SharedBank().GetCue("TestCue"));
    std::unique_ptr<Cue> cueB(SharedBank().GetCue("TestCue"));
    std::unique_ptr<Cue> cueC(SharedBank().GetCue("TestCue"));

    cueA->Play();
    cueB->Play();
    cueC->Play();
    ASSERT_TRUE(cueA->getIsPlayingProperty());
    ASSERT_TRUE(cueB->getIsPlayingProperty());
    ASSERT_TRUE(cueC->getIsPlayingProperty());

    cat.Stop(AudioStopOptions::Immediate);

    EXPECT_TRUE(cueA->getIsStoppedProperty());
    EXPECT_TRUE(cueB->getIsStoppedProperty());
    EXPECT_TRUE(cueC->getIsStoppedProperty());
}

// P9-CATEGORY-003: Pause()/Resume() don't have Stop()'s reentrant-mutation bug (neither cascades
// into UnregisterCue()), so this wouldn't fail against the pre-P9-CATEGORY-001 code -- it's a
// completeness/regression test for the multi-cue case, not a bug reproduction.
TEST(AudioCategoryTest, PauseAndResumeAffectAllActiveCuesInCategory)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    std::unique_ptr<Cue> cueA(SharedBank().GetCue("TestCue"));
    std::unique_ptr<Cue> cueB(SharedBank().GetCue("TestCue"));
    std::unique_ptr<Cue> cueC(SharedBank().GetCue("TestCue"));

    cueA->Play();
    cueB->Play();
    cueC->Play();

    cat.Pause();
    EXPECT_TRUE(cueA->getIsPausedProperty());
    EXPECT_TRUE(cueB->getIsPausedProperty());
    EXPECT_TRUE(cueC->getIsPausedProperty());

    cat.Resume();
    EXPECT_TRUE(cueA->getIsPlayingProperty());
    EXPECT_TRUE(cueB->getIsPlayingProperty());
    EXPECT_TRUE(cueC->getIsPlayingProperty());

    cat.Stop(AudioStopOptions::Immediate);
}

// T-4D: AudioEngine::SetCategoryVolumeInternal must re-apply the new volume to already-active
// cue instances, not just affect future Play() calls. This needs a real SoundEffectInstance in
// Cue::active_ (SharedVolBank's cue, unlike SharedBank's TestCue, has a real WaveBank behind
// it), so it's a separate fixture/test from PauseResumeStopRouteToRealActiveCueInCategory above.
TEST(AudioCategoryTest, SetVolumeReappliesToAlreadyPlayingCueInstance)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        AudioCategory cat = SharedEngine().GetCategory("Default");
        cat.SetVolume(1.0f); // known baseline -- SharedEngine's "Default" category is shared
                             // with other tests in this file that also call SetVolume on it
        std::unique_ptr<Cue> cue(SharedVolBank().GetCue("VolCue"));
        cue->Play();

        const auto volumesAtPlay = CueTestAccess::ActiveInstanceVolumes(*cue);
        if (volumesAtPlay.empty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        EXPECT_NO_THROW(cat.SetVolume(0.5f));
        const auto volumesAfterSetVolume = CueTestAccess::ActiveInstanceVolumes(*cue);
        ASSERT_EQ(volumesAfterSetVolume.size(), volumesAtPlay.size());
        for (std::size_t i = 0; i < volumesAtPlay.size(); ++i)
            EXPECT_LT(volumesAfterSetVolume[i], volumesAtPlay[i]);

        cue->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P9-CATEGORY-004: same as SetVolumeReappliesToAlreadyPlayingCueInstance above, but with two
// simultaneously-active real cue instances in the category, to prove the volume change reaches
// every one of them, not just whichever happens to be first in AudioEngine::activeCues.
// ApplyCategoryVolume() doesn't mutate activeCues, so this wouldn't fail against the
// pre-P9-CATEGORY-001 code either -- a completeness test, not a bug reproduction.
TEST(AudioCategoryTest, SetVolumeAppliesToAllActivePlayingCueInstancesInCategory)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        AudioCategory cat = SharedEngine().GetCategory("Default");
        cat.SetVolume(1.0f); // known baseline, see SetVolumeReappliesToAlreadyPlayingCueInstance

        std::unique_ptr<Cue> cueA(SharedVolBank().GetCue("VolCue"));
        std::unique_ptr<Cue> cueB(SharedVolBank().GetCue("VolCue"));
        cueA->Play();
        cueB->Play();

        const auto volAAtPlay = CueTestAccess::ActiveInstanceVolumes(*cueA);
        const auto volBAtPlay = CueTestAccess::ActiveInstanceVolumes(*cueB);
        if (volAAtPlay.empty() || volBAtPlay.empty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        EXPECT_NO_THROW(cat.SetVolume(0.5f));

        const auto volAAfter = CueTestAccess::ActiveInstanceVolumes(*cueA);
        const auto volBAfter = CueTestAccess::ActiveInstanceVolumes(*cueB);
        ASSERT_EQ(volAAfter.size(), volAAtPlay.size());
        ASSERT_EQ(volBAfter.size(), volBAtPlay.size());
        for (std::size_t i = 0; i < volAAtPlay.size(); ++i)
            EXPECT_LT(volAAfter[i], volAAtPlay[i]);
        for (std::size_t i = 0; i < volBAtPlay.size(); ++i)
            EXPECT_LT(volBAfter[i], volBAtPlay[i]);

        cueA->Stop(AudioStopOptions::Immediate);
        cueB->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// ===================== Equals / GetHashCode / operators =====================

TEST(AudioCategoryTest, EqualsTrueForSameName)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    EXPECT_TRUE(a.Equals(b));
}

TEST(AudioCategoryTest, EqualsFalseForDifferentName)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory c = SharedEngine().GetCategory("Combat");
    EXPECT_FALSE(a.Equals(c));
}

TEST(AudioCategoryTest, GetHashCodeConsistentForSameName)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(AudioCategoryTest, EqualityOperatorMatchesEquals)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    AudioCategory c = SharedEngine().GetCategory("Combat");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(AudioCategoryTest, InequalityOperatorMatchesNegatedEquals)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    AudioCategory c = SharedEngine().GetCategory("Combat");
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}
