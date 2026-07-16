// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "AudioEngineTestAccess.hpp"
#include "CueTestAccess.hpp"
#include "System/Environment.hpp"

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
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioEngineTestAccess;
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

    constexpr const char* kP9StopLongWaveBankName = "P9StopCategoryLongWaveBank";

    // Same layout as BuildVolXwbFixtureBytes, but with a full second of audio instead of 200
    // bytes (~1ms) -- P9-STOP-008's authored-stop-tail check needs the tail to still be reliably
    // audible at assertion time, not racing a near-instant natural finish under full-suite load
    // (same rationale as CueTests.cpp's BuildLongXwbFixtureBytes/"LongCue" for XA-6).
    std::vector<uint8_t> BuildP9StopLongXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 44100u * 2u; // 1 second, mono 16-bit @ 44100Hz
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
        AppendPadded(data, kP9StopLongWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)            // format tag: PCM
            | (1u << 2)       // channels: mono
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

    // Fade duration authored on "P9StopCategoryLongCue" below (P9-STOP-010) -- far shorter than
    // the underlying wave's 1 second, so reaching Stopped this fast can only be the authored fade
    // timer elapsing, never the wave naturally finishing.
    constexpr uint16_t kP9StopCategoryLongCueFadeOutMS = 300;

    // Same layout as BuildVolXsbFixtureBytes, but referencing kP9StopLongWaveBankName and named
    // "P9StopCategoryLongCue". Unlike a simple cue, this is a COMPLEX cue (CUE_FLAG_SINGLE_SOUND
    // set) authoring a real fadeOutMS (P9-STOP-010) -- a simple cue's format has no such field at
    // all (always 0), so it could never exercise real Stop(AsAuthored) fade timing.
    std::vector<uint8_t> BuildP9StopLongXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueComplexOffset   = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "P9StopCategoryLongCue";

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
        AppendU8(data, 1);  // wavebankCount
        AppendU16(data, 1); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, -1); // cueSimpleOffset
        AppendS32(data, static_cast<int32_t>(cueComplexOffset));
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, static_cast<int32_t>(wavebankNameOffset));
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "P9StopCategoryLongSoundBank", bankNameSize);
        AppendPadded(data, kP9StopLongWaveBankName, 64);

        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        // Complex cue "P9StopCategoryLongCue": single-sound (CUE_FLAG_SINGLE_SOUND), sbCode
        // points directly at the one real sound above, authoring a real fadeOutMS.
        AppendU8(data, 0x04); // flags: CUE_FLAG_SINGLE_SOUND
        AppendU32(data, soundOffset); // sbCode
        AppendU32(data, 0);   // transitionOffset
        AppendU8(data, 0xFF); // instanceLimit
        AppendU16(data, 0);   // fadeInMS
        AppendU16(data, kP9StopCategoryLongCueFadeOutMS); // fadeOutMS
        AppendU8(data, 0);    // maxInstanceBehavior

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    WaveBank& SharedP9StopLongWaveBank()
    {
        static WaveBank wb(&SharedEngine(), WriteFixture(
            "cna_audio_category_test", "p9stop_long.xwb", BuildP9StopLongXwbFixtureBytes()));
        return wb;
    }

    SoundBank& SharedP9StopLongBank()
    {
        (void)SharedP9StopLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_audio_category_test", "p9stop_long.xsb", BuildP9StopLongXsbFixtureBytes()));
        return bank;
    }

    // ── P9-CATEGORY-005/006/007/008: instanceLimit + maxInstanceBehavior + fade fixture ───────
    //
    // A dedicated engine/wavebank/soundbank, independent from SharedEngine() above, with three
    // categories each configured for one of the maxInstanceBehavior values CNA implements:
    //   0 "CatFail"     instanceLimit=1, FAIL                  -- exceeding the limit rejects the
    //                    new cue outright.
    //   1 "CatReplace"  instanceLimit=1, REPLACE_OLDEST, both fadeInMS/fadeOutMS=kCategoryLimit-
    //                    FadeMS -- exceeding the limit fades out the oldest active cue and fades
    //                    the new one in.
    //   2 "CatPriority" instanceLimit=2, REPLACE_LOWEST_PRIORITY, no fade -- exceeding the limit
    //                    evicts whichever active cue has the lowest XsbSound::priority,
    //                    regardless of play order (distinguishes this from REPLACE_OLDEST).

    void AppendCategoryEx(std::vector<uint8_t>& buf, uint8_t instanceLimit, uint16_t fadeInMS,
                          uint16_t fadeOutMS, uint8_t maxInstanceBehavior)
    {
        AppendU8(buf, instanceLimit);
        AppendU16(buf, fadeInMS);
        AppendU16(buf, fadeOutMS);
        // XactParser.cpp reads this byte and shifts right by 3 -- real .xgs files store the
        // behavior value in the byte's upper bits, so encode it left-shifted by 3 here to
        // round-trip correctly.
        AppendU8(buf, static_cast<uint8_t>(maxInstanceBehavior << 3));
        AppendU16(buf, 0xFFFF); // parentIndex (none)
        AppendU8(buf, 0xFF);    // volume byte
        AppendU8(buf, 0);       // visibility
    }

    constexpr uint16_t kCategoryLimitFadeMS = 60;

    std::vector<uint8_t> BuildCategoryLimitXgsFixtureBytes()
    {
        constexpr uint32_t headerSize       = 65;
        constexpr uint32_t categoryDataSize = 10;
        constexpr uint32_t categoryCount    = 3;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryCount * categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset;
        const std::string name0 = "CatFail";
        const std::string name1 = "CatReplace";
        const std::string name2 = "CatPriority";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(name0.size()) + 1 +
            static_cast<uint32_t>(name1.size()) + 1 +
            static_cast<uint32_t>(name2.size()) + 1;

        std::vector<uint8_t> data;
        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, categoryCount);
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

        AppendCategoryEx(data, 1, 0, 0, 0);                                            // CatFail
        AppendCategoryEx(data, 1, kCategoryLimitFadeMS, kCategoryLimitFadeMS, 2);       // CatReplace
        AppendCategoryEx(data, 2, 0, 0, 4);                                            // CatPriority

        AppendCStr(data, name0);
        AppendCStr(data, name1);
        AppendCStr(data, name2);

        return data;
    }

    constexpr const char* kCategoryLimitWaveBankName = "CategoryLimitWaveBank";

    // Same 1-second mono 16-bit silence pattern as BuildP9StopLongXwbFixtureBytes -- long enough
    // that natural completion can never race the short category fade windows exercised below.
    std::vector<uint8_t> BuildCategoryLimitXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 44100u * 2u; // 1 second, mono 16-bit @ 44100Hz
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
        AppendPadded(data, kCategoryLimitWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)            // format tag: PCM
            | (1u << 2)       // channels: mono
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

    struct CategoryLimitSoundDef { std::string cueName; uint16_t categoryIndex; uint8_t priority; };

    const std::vector<CategoryLimitSoundDef>& CategoryLimitSoundDefs()
    {
        static const std::vector<CategoryLimitSoundDef> defs = {
            {"CatFailCueA",        0,   0},
            {"CatFailCueB",        0,   0},
            {"CatReplaceCueA",     1,   0},
            {"CatReplaceCueB",     1,   0},
            {"CatPriorityCueHigh", 2, 200}, // survives -- highest priority among CatPriority cues
            {"CatPriorityCueLow",  2,  10}, // evicted -- lowest priority among CatPriority cues
            {"CatPriorityCueNew",  2, 100}, // triggers the eviction once played third
        };
        return defs;
    }

    // One simple sound + one simple cue per CategoryLimitSoundDefs() entry, all referencing wave
    // index 0 of the single wavebank above (same layout as BuildVolXsbFixtureBytes, generalized
    // to N sounds/cues instead of 1).
    std::vector<uint8_t> BuildCategoryLimitXsbFixtureBytes()
    {
        const auto& defs = CategoryLimitSoundDefs();
        const uint16_t soundCount = static_cast<uint16_t>(defs.size());

        constexpr uint32_t headerSize            = 74;
        constexpr uint32_t bankNameSize           = 64;
        constexpr uint32_t baseOffset             = headerSize + bankNameSize;
        constexpr uint32_t soundSize              = 12;
        constexpr uint32_t cueSimpleEntrySize     = 5;
        constexpr uint32_t cueNameIndexEntrySize  = 6;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + soundCount * soundSize;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + soundCount * cueSimpleEntrySize;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + soundCount * cueNameIndexEntrySize;

        std::vector<uint32_t> nameAbsOffsets(soundCount);
        {
            uint32_t running = cueNameStrOffset;
            for (uint16_t i = 0; i < soundCount; ++i)
            {
                nameAbsOffsets[i] = running;
                running += static_cast<uint32_t>(defs[i].cueName.size()) + 1;
            }
        }

        std::vector<uint32_t> soundAbsOffsets(soundCount);
        for (uint16_t i = 0; i < soundCount; ++i)
            soundAbsOffsets[i] = soundOffset + i * soundSize;

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, soundCount); // cueSimpleCount
        AppendU16(data, 0);          // cueComplexCount
        AppendU16(data, 0);          // unknown
        AppendU16(data, 0);          // cueTotalAlign
        AppendU8(data, 1);           // wavebankCount
        AppendU16(data, soundCount); // soundCount
        AppendU16(data, 0);          // cueNameLength
        AppendU16(data, 0);          // unknown

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

        AppendPadded(data, "CategoryLimitSoundBank", bankNameSize);
        AppendPadded(data, kCategoryLimitWaveBankName, 64);

        for (const auto& def : defs)
        {
            AppendU8(data, 0);                  // flags
            AppendU16(data, def.categoryIndex); // categoryIndex
            AppendU8(data, 0xFF);               // volume raw byte
            AppendU16(data, 0);                 // pitchCents
            AppendU8(data, def.priority);       // priority
            AppendU16(data, 0);                 // soundLength (skipped)
            AppendU16(data, 0);                 // waveIdx
            AppendU8(data, 0);                  // wbIdx
        }

        for (uint16_t i = 0; i < soundCount; ++i)
        {
            AppendU8(data, 0); // flags
            AppendU32(data, soundAbsOffsets[i]); // sbCode
        }

        for (uint16_t i = 0; i < soundCount; ++i)
        {
            AppendU32(data, nameAbsOffsets[i]);
            AppendU16(data, 0); // unknown
        }

        for (const auto& def : defs)
            AppendCStr(data, def.cueName);

        return data;
    }

    AudioEngine& CategoryLimitEngine()
    {
        static const std::string path =
            WriteFixture("cna_audio_category_limit_test", "fixture.xgs",
                         BuildCategoryLimitXgsFixtureBytes());
        static AudioEngine engine(path);
        return engine;
    }

    WaveBank& CategoryLimitWaveBank()
    {
        static WaveBank wb(&CategoryLimitEngine(), WriteFixture(
            "cna_audio_category_limit_test", "fixture.xwb", BuildCategoryLimitXwbFixtureBytes()));
        return wb;
    }

    SoundBank& CategoryLimitBank()
    {
        (void)CategoryLimitWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&CategoryLimitEngine(), WriteFixture(
            "cna_audio_category_limit_test", "fixture.xsb", BuildCategoryLimitXsbFixtureBytes()));
        return bank;
    }

    // ── P12-CATEGORY-001: category parent/child hierarchy fixture ────────────────────────────
    //
    // Two categories: "HierParent" (index 0, no parent) and "HierChild" (index 1,
    // parentIndex=0). One cue, "HierChildCue", belongs to "HierChild" -- used to verify that
    // SetVolume/Pause/Resume/Stop on the PARENT category ("HierParent") reaches a cue whose own
    // category is a DESCENDANT ("HierChild"), matching FACT.c's real
    // FACTAudioEngine_SetVolume/_Pause/_Stop + FACT_INTERNAL_IsInCategory behavior.

    void AppendHierarchyCategory(std::vector<uint8_t>& buf, uint16_t parentIndex, uint8_t volumeByte)
    {
        AppendU8(buf, 0xFF);         // instanceLimit (unlimited)
        AppendU16(buf, 0);           // fadeInMS
        AppendU16(buf, 0);           // fadeOutMS
        AppendU8(buf, 0);            // maxInstanceBehavior (skip, FAIL)
        AppendU16(buf, parentIndex);
        AppendU8(buf, volumeByte);
        AppendU8(buf, 0);            // visibility
    }

    std::vector<uint8_t> BuildCategoryHierarchyXgsFixtureBytes()
    {
        constexpr uint32_t headerSize       = 65;
        constexpr uint32_t categoryDataSize = 10;
        constexpr uint32_t categoryCount    = 2;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryCount * categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset;
        const std::string name0 = "HierParent";
        const std::string name1 = "HierChild";
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

        AppendU16(data, categoryCount);
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

        AppendHierarchyCategory(data, 0xFFFF, 0xFF); // "HierParent", no parent
        AppendHierarchyCategory(data, 0,      0xFF); // "HierChild", parent = HierParent

        AppendCStr(data, name0);
        AppendCStr(data, name1);

        return data;
    }

    constexpr const char* kHierarchyWaveBankName = "HierarchyWaveBank";

    // Same layout as BuildVolXwbFixtureBytes above -- one mono 16-bit PCM entry, 200 bytes of
    // silence.
    std::vector<uint8_t> BuildCategoryHierarchyXwbFixtureBytes()
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
        AppendPadded(data, kHierarchyWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)            // format tag: PCM
            | (1u << 2)       // channels: mono
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

    // Same layout as BuildVolXsbFixtureBytes above, but the sound's categoryIndex is 1
    // ("HierChild") instead of 0.
    std::vector<uint8_t> BuildCategoryHierarchyXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "HierChildCue";

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

        AppendPadded(data, "HierarchySoundBank", bankNameSize);
        AppendPadded(data, kHierarchyWaveBankName, 64);

        // Sound: simple, categoryIndex=1 ("HierChild").
        AppendU8(data, 0);    // flags
        AppendU16(data, 1);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        // Simple cue "HierChildCue", pointing at the sound above.
        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    AudioEngine& CategoryHierarchyEngine()
    {
        static const std::string path =
            WriteFixture("cna_audio_category_hierarchy_test", "fixture.xgs",
                         BuildCategoryHierarchyXgsFixtureBytes());
        static AudioEngine engine(path);
        return engine;
    }

    WaveBank& CategoryHierarchyWaveBank()
    {
        static WaveBank wb(&CategoryHierarchyEngine(), WriteFixture(
            "cna_audio_category_hierarchy_test", "fixture.xwb", BuildCategoryHierarchyXwbFixtureBytes()));
        return wb;
    }

    SoundBank& CategoryHierarchyBank()
    {
        (void)CategoryHierarchyWaveBank(); // must be registered before GetCue()/Play()
        static SoundBank bank(&CategoryHierarchyEngine(), WriteFixture(
            "cna_audio_category_hierarchy_test", "fixture.xsb", BuildCategoryHierarchyXsbFixtureBytes()));
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
    // P9-LIFECYCLE-013: matches real FACT -- pausing never clears IsPlaying.
    EXPECT_TRUE(cue->getIsPlayingProperty());

    cat.Resume();
    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_FALSE(cue->getIsPausedProperty());

    EXPECT_NO_THROW(cat.SetVolume(0.5f));

    cat.Stop(AudioStopOptions::Immediate);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

// P9-STOP-008: AudioCategory::Stop(AsAuthored) must leave a real active cue's release tail
// playing (State::Stopping), not hard-stop it immediately -- SharedBank()'s wavebank-less
// "TestCue" above can't exercise this (Cue::active_ stays empty), so this uses a real
// WaveBank-backed fixture instead, same rationale as CueTests.cpp's
// StopAsAuthoredLeavesTrackPlayingButStopImmediateHardStopsRightAway. Needs the 1-second "Long"
// fixture specifically, not SharedVolBank's 200-byte (~1.13ms) "VolCue" -- under full-suite load
// the short fixture can finish naturally between Play() and the assertion below, which would
// reconcile State::Stopping straight to Stopped before this test ever observes it (confirmed
// flaky empirically with the short fixture: ~30-40% failure rate over repeated full-suite runs).
TEST(AudioCategoryTest, StopAsAuthoredOnCategoryLeavesRealActiveCueStoppingNotStopped)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioCategory cat = SharedEngine().GetCategory("Default");
        std::unique_ptr<Cue> cue(SharedP9StopLongBank().GetCue("P9StopCategoryLongCue"));
        cue->Play();

        if (!CueTestAccess::ActiveInstance(*cue, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        cat.Stop(AudioStopOptions::AsAuthored);

        EXPECT_TRUE(cue->getIsStoppingProperty());
        EXPECT_FALSE(cue->getIsStoppedProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*cue, 0), nullptr);

        cue->Stop(AudioStopOptions::Immediate); // clean up rather than leaking a Stopping cue
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
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
    // P9-LIFECYCLE-013: matches real FACT -- pausing never clears IsPlaying.
    EXPECT_TRUE(cueA->getIsPlayingProperty());
    EXPECT_TRUE(cueB->getIsPlayingProperty());
    EXPECT_TRUE(cueC->getIsPlayingProperty());

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
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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

        EXPECT_NO_THROW(cat.SetVolume(0.2f));
        const auto volumesAfterSetVolume = CueTestAccess::ActiveInstanceVolumes(*cue);
        ASSERT_EQ(volumesAfterSetVolume.size(), volumesAtPlay.size());
        // P12-CATEGORY-001: exact values, not just "decreased" -- SetVolume() multiplies its
        // argument by the category's own authored base volume (matches FACT.c's real
        // FACTAudioEngine_SetVolume formula, `currentVolume = categories[n].volume * volume`,
        // not a raw overwrite). "Default"'s own authored volume byte is 0xFF (amplitude
        // ~1.9977), same as VolCue's own sound volume byte -- SetVolume(1.0) gives
        // categoryVolume ~1.9977, which combined with the sound's own ~1.9977 saturates the
        // [0,1] clamp to exactly 1.0. SetVolume(0.2) gives categoryVolume ~0.39955 (1.9977*0.2),
        // which combined with the sound's ~1.9977 is ~0.79819 -- still comfortably below 1.0, so
        // this is the smallest round SetVolume() argument that stays discriminating (doesn't
        // saturate) against this fixture; 0.5 or even 0.3 both still saturate to 1.0 with this
        // formula and wouldn't prove SetVolume's new value took effect.
        for (std::size_t i = 0; i < volumesAtPlay.size(); ++i)
        {
            EXPECT_FLOAT_EQ(volumesAtPlay[i], 1.0f);
            EXPECT_NEAR(volumesAfterSetVolume[i], 0.79819f, 1e-4f);
        }

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
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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

        EXPECT_NO_THROW(cat.SetVolume(0.2f));

        const auto volAAfter = CueTestAccess::ActiveInstanceVolumes(*cueA);
        const auto volBAfter = CueTestAccess::ActiveInstanceVolumes(*cueB);
        ASSERT_EQ(volAAfter.size(), volAAtPlay.size());
        ASSERT_EQ(volBAfter.size(), volBAtPlay.size());
        // P12-CATEGORY-001: exact values (see SetVolumeReappliesToAlreadyPlayingCueInstance's
        // identical fixture/derivation, updated the same way for the same reason) -- 1.0
        // (clamped) before, ~0.79819 (unclamped) after.
        for (std::size_t i = 0; i < volAAtPlay.size(); ++i)
        {
            EXPECT_FLOAT_EQ(volAAtPlay[i], 1.0f);
            EXPECT_NEAR(volAAfter[i], 0.79819f, 1e-4f);
        }
        for (std::size_t i = 0; i < volBAtPlay.size(); ++i)
        {
            EXPECT_FLOAT_EQ(volBAtPlay[i], 1.0f);
            EXPECT_NEAR(volBAfter[i], 0.79819f, 1e-4f);
        }

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

// ===================== P9-CATEGORY-005/006/007/008: instanceLimit / maxInstanceBehavior =====

// FAIL (0): once "CatFail" (instanceLimit=1) already has one cue playing, a second cue in the
// same category must be rejected outright -- matches handle_instance_limit() calling
// FACTCue_Stop(cue, IMMEDIATE) on the *new* cue (FACT_internal.c). The first cue must be
// completely unaffected.
TEST(AudioCategoryTest, InstanceLimitFailRejectsNewCueOnceLimitReached)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cueA(CategoryLimitBank().GetCue("CatFailCueA"));
        cueA->Play();
        if (!CueTestAccess::ActiveInstance(*cueA, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        ASSERT_TRUE(cueA->getIsPlayingProperty());

        std::unique_ptr<Cue> cueB(CategoryLimitBank().GetCue("CatFailCueB"));
        cueB->Play();

        EXPECT_TRUE(cueB->getIsStoppedProperty());
        EXPECT_FALSE(cueB->getIsPlayingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cueB, 0), nullptr);

        // cueA must be completely unaffected by cueB's rejected Play().
        EXPECT_TRUE(cueA->getIsPlayingProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*cueA, 0), nullptr);

        cueA->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P12-PAUSE-001: real FACT's instanceCount is only decremented once a sound is actually
// destroyed (FACT_INTERNAL_DestroySound), never at Pause -- FACT_STATE_PAUSED never clears
// FACT_STATE_PLAYING (FACT.c:2647/2651), so a Paused cue still counts toward its category's
// instanceLimit. This was flagged as a possible gap by a Phase 12 audit pass, but investigating
// CNA's own Cue::Pause() (Cue.cpp) shows it never changes state_ away from State::Playing --
// pause is tracked entirely via the independent paused_ bool (matching NEXT.md's own documented
// invariant, "Cue::Pause()/IsPaused never clear/depend on IsPlaying"). This test empirically
// confirms AudioEngine::CheckCategoryInstanceLimit's `state_ == Playing` check therefore already
// counts a Paused cue correctly, with no code change needed -- a "confirmed correct, not a bug"
// regression lock, not a new fix.
TEST(AudioCategoryTest, InstanceLimitStillCountsAPausedCue)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cueA(CategoryLimitBank().GetCue("CatFailCueA"));
        cueA->Play();
        if (!CueTestAccess::ActiveInstance(*cueA, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        ASSERT_TRUE(cueA->getIsPlayingProperty());

        cueA->Pause();
        ASSERT_TRUE(cueA->getIsPlayingProperty()); // P9-LIFECYCLE-013: independent flags
        ASSERT_TRUE(cueA->getIsPausedProperty());

        // "CatFail" has instanceLimit=1 -- cueA being Paused (not Stopped) must still count
        // toward that limit, so cueB must still be rejected exactly as if cueA were unpaused.
        std::unique_ptr<Cue> cueB(CategoryLimitBank().GetCue("CatFailCueB"));
        cueB->Play();

        EXPECT_TRUE(cueB->getIsStoppedProperty());
        EXPECT_FALSE(cueB->getIsPlayingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cueB, 0), nullptr);

        // cueA must be completely unaffected -- still playing (and still paused).
        EXPECT_TRUE(cueA->getIsPlayingProperty());
        EXPECT_TRUE(cueA->getIsPausedProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*cueA, 0), nullptr);

        cueA->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// REPLACE_OLDEST (2): once "CatReplace" (instanceLimit=1) already has one cue playing, a second
// cue in the same category must fade out the first (over the category's authored fadeOutMS,
// State::Stopping) and fade itself in from silence (over the category's authored fadeInMS) --
// matches handle_instance_limit()'s FACT_INTERNAL_BeginFadeOut(victim, category->fadeOutMS) and
// play_sound()'s SOUND_STATE_FADE_IN using category->fadeInMS (FACT_internal.c).
TEST(AudioCategoryTest, InstanceLimitReplaceOldestFadesOutVictimAndFadesInNewCue)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cueA(CategoryLimitBank().GetCue("CatReplaceCueA"));
        cueA->Play();
        if (!CueTestAccess::ActiveInstance(*cueA, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        ASSERT_TRUE(cueA->getIsPlayingProperty());

        std::unique_ptr<Cue> cueB(CategoryLimitBank().GetCue("CatReplaceCueB"));
        cueB->Play();

        // cueA is immediately Stopping (its fade-out ramp has just started); cueB starts
        // playing from silence (its fade-in ramp starts at volume 0.0 exactly).
        EXPECT_TRUE(cueA->getIsStoppingProperty());
        EXPECT_FALSE(cueA->getIsStoppedProperty());
        const auto volBAtPlay = CueTestAccess::ActiveInstanceVolumes(*cueB);
        ASSERT_FALSE(volBAtPlay.empty());
        EXPECT_FLOAT_EQ(volBAtPlay[0], 0.0f);

        // Comfortably past kCategoryLimitFadeMS for both ramps.
        std::this_thread::sleep_for(std::chrono::milliseconds(kCategoryLimitFadeMS + 150));

        EXPECT_TRUE(cueA->getIsStoppedProperty());
        EXPECT_FALSE(cueA->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cueA, 0), nullptr);

        ASSERT_TRUE(cueB->getIsPlayingProperty());
        const auto volBAfter = CueTestAccess::ActiveInstanceVolumes(*cueB);
        ASSERT_FALSE(volBAfter.empty());
        // P11-TEST-001: exact value, not just ">0.9" -- CatReplaceCueB's sound volume byte
        // (0xFF, amplitude ~1.9977) combined with the "CatReplace" category's own authored
        // volume byte (also 0xFF, ~1.9977) is ~3.99 pre-clamp, so the fully-faded-in target
        // volume clamps to exactly 1.0.
        EXPECT_FLOAT_EQ(volBAfter[0], 1.0f);

        cueB->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// REPLACE_LOWEST_PRIORITY (4): "CatPriority" (instanceLimit=2, no fade) has two cues already
// playing (High, priority=200; Low, priority=10) when a third is played. FACT's
// handle_instance_limit() searches for the lowest-priority *currently playing* cue in the
// category regardless of play order -- Low must be evicted even though High was played first
// (which would be the REPLACE_OLDEST victim instead), proving this is genuinely priority-based,
// not just oldest-first. No fade is authored on this category, so eviction is a synchronous
// immediate stop (Cue::ForceFadeOutForInstanceLimit's fadeOutMS==0 branch).
TEST(AudioCategoryTest, InstanceLimitReplaceLowestPriorityEvictsLowestPriorityRegardlessOfPlayOrder)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cueHigh(CategoryLimitBank().GetCue("CatPriorityCueHigh"));
        cueHigh->Play();
        if (!CueTestAccess::ActiveInstance(*cueHigh, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        std::unique_ptr<Cue> cueLow(CategoryLimitBank().GetCue("CatPriorityCueLow"));
        cueLow->Play();
        ASSERT_TRUE(cueLow->getIsPlayingProperty()); // limit is 2, both fit without eviction yet

        std::unique_ptr<Cue> cueNew(CategoryLimitBank().GetCue("CatPriorityCueNew"));
        cueNew->Play();

        // Low (lowest priority among the two active cues) is evicted immediately (no fade
        // authored on this category); High (higher priority) is untouched; New plays normally.
        EXPECT_TRUE(cueLow->getIsStoppedProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cueLow, 0), nullptr);

        EXPECT_TRUE(cueHigh->getIsPlayingProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*cueHigh, 0), nullptr);

        EXPECT_TRUE(cueNew->getIsPlayingProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*cueNew, 0), nullptr);

        cueHigh->Stop(AudioStopOptions::Immediate);
        cueNew->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// ===================== P12-CATEGORY-001: category parent/child hierarchy =====================
//
// FACT.c's FACTAudioEngine_Pause/_Stop use FACT_INTERNAL_IsInCategory (walks a cue's own
// category up its parent chain), and FACTAudioEngine_SetVolume recursively cascades to every
// DIRECT child category -- so an operation on "HierParent" must reach a cue whose own category
// is "HierChild" (a child of "HierParent"), even though the cue was never directly assigned to
// "HierParent" itself.

TEST(AudioCategoryTest, PauseOnParentCategoryPausesCueInChildCategory)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cue(CategoryHierarchyBank().GetCue("HierChildCue"));
        cue->Play();
        if (!CueTestAccess::ActiveInstance(*cue, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        ASSERT_TRUE(cue->getIsPlayingProperty());
        ASSERT_FALSE(cue->getIsPausedProperty());

        AudioCategory parent = CategoryHierarchyEngine().GetCategory("HierParent");
        parent.Pause();

        EXPECT_TRUE(cue->getIsPlayingProperty()); // P9-LIFECYCLE-013: independent flags
        EXPECT_TRUE(cue->getIsPausedProperty());

        parent.Resume();
        EXPECT_FALSE(cue->getIsPausedProperty());

        cue->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

TEST(AudioCategoryTest, StopOnParentCategoryStopsCueInChildCategory)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cue(CategoryHierarchyBank().GetCue("HierChildCue"));
        cue->Play();
        if (!CueTestAccess::ActiveInstance(*cue, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        ASSERT_TRUE(cue->getIsPlayingProperty());

        AudioCategory parent = CategoryHierarchyEngine().GetCategory("HierParent");
        parent.Stop(AudioStopOptions::Immediate);

        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsPlayingProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// SetVolume's own cascade is exact-value verifiable (unlike Pause/Stop, which are boolean state):
// FACTAudioEngine_SetVolume(parent, 0.5) sets categoryVolumes[parent] = authored(parent)*0.5,
// then recursively calls SetVolume(child, categoryVolumes[child]_old) -- so
// categoryVolumes[child]_new = authored(child) * categoryVolumes[child]_old. Both "HierParent"
// and "HierChild" have authored volume byte 0xFF (amplitude ~1.9977); before any SetVolume call,
// categoryVolumes[child] == its own authored value (~1.9977, seeded at Init()), so after
// SetVolume(parent, 0.5): categoryVolumes[child] = 1.9977 * 1.9977 = ~3.99091, clamped later at
// the per-cue mix stage. Combined with HierChildCue's own sound volume byte (also 0xFF,
// ~1.9977), the final live instance volume is clamp(1.9977*3.99091, 0, 1) = 1.0 (saturates) --
// still enough to prove the cascade *reached* the child (its categoryVolumes entry changed at
// all), verified indirectly via GetCategoryVolume since AudioCategory itself has no volume
// getter (matches real XNA -- Volume is a command-only property in FNA's own AudioCategory.cs).
TEST(AudioCategoryTest, SetVolumeOnParentCategoryCascadesToChildCategory)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    AudioEngine& engine = CategoryHierarchyEngine();
    // "HierChild" is authored as category index 1 in BuildCategoryHierarchyXgsFixtureBytes above
    // (index 0 = "HierParent", index 1 = "HierChild") -- known and fixed by this test's own
    // fixture, no name->index lookup needed.
    constexpr unsigned short kHierChildIdx = 1;

    const float childVolBefore = AudioEngineTestAccess::GetCategoryVolume(engine, kHierChildIdx);

    AudioCategory parent = engine.GetCategory("HierParent");
    parent.SetVolume(0.5f);

    const float childVolAfter = AudioEngineTestAccess::GetCategoryVolume(engine, kHierChildIdx);

    // The cascade must have changed the child's own stored category volume -- before this fix,
    // SetVolume never touched anything outside the exact target category index at all.
    EXPECT_NE(childVolAfter, childVolBefore);
    // Exact value: authored("HierChild") * childVolBefore = 1.9977355869281581^2.
    EXPECT_NEAR(childVolAfter, 1.9977355869281581f * childVolBefore, 1e-4f);

    parent.SetVolume(1.0f); // restore a known baseline for any other test sharing this engine
}
